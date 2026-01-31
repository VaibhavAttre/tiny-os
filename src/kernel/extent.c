#include <kernel/extent.h>
#include <stdint.h>
#include <kernel/btree.h>
#include <kernel/fs.h>
#include <kernel/printf.h>
#include <kernel/buf.h>
#include <kernel/string.h>
#include <kernel/tree.h>

#define MAX_DEFERRED 64

static struct extent deferred[MAX_DEFERRED];
static int deferred_n = 0;
static int extent_meta = 0;

static uint32_t extent_btree_checksum(const struct btree_node *node);

static void extent_meta_enter(void) {
    extent_meta++;
}

static void extent_meta_exit(void) {
    if (extent_meta > 0) {
        extent_meta--;
    }
}

int extent_meta_active(void) {
    return extent_meta != 0;
}

static int extent_leaf_read(uint32_t root, struct btree_node *out) {
    if (root == 0 || root >= sb.nblocks) {
        return -1;
    }
    struct buf *bp = bread(root);
    memmove(out, bp->data, sizeof(*out));
    brelse(bp);

    if (out->hdr.magic != BTREE_MAGIC) {
        return -1;
    }
    if (out->hdr.type != BTREE_TYPE_NODE) {
        return -1;
    }
    if (out->hdr.level != 0 || out->hdr.nkeys > BTREE_ORDER) {
        return -1;
    }
    if (out->hdr.logical != 0 && out->hdr.logical != root) {
        return -1;
    }
    if (extent_btree_checksum(out) != out->hdr.checksum) {
        return -1;
    }
    return 0;
}

static int extent_leaf_write(uint32_t root, struct btree_node *node) {
    if (root == 0 || root >= sb.nblocks) {
        return -1;
    }
    node->hdr.magic = BTREE_MAGIC;
    node->hdr.type = BTREE_TYPE_NODE;
    node->hdr.logical = root;
    node->hdr.level = 0;
    node->hdr.generation = sb.generation + 1;
    node->hdr.checksum = extent_btree_checksum(node);

    struct buf *bp = bread(root);
    memmove(bp->data, node, sizeof(*node));
    bwrite(bp);
    brelse(bp);
    return 0;
}

static int extent_leaf_remove(uint32_t root, uint64_t key) {
    struct btree_node node;
    if (extent_leaf_read(root, &node) < 0) {
        return -1;
    }

    uint16_t n = node.hdr.nkeys;
    uint16_t i = 0;
    while (i < n && node.keys[i].key < key) {
        i++;
    }
    if (i == n || node.keys[i].key != key) {
        return 0;
    }
    for (uint16_t j = i + 1; j < n; j++) {
        node.keys[j - 1] = node.keys[j];
    }
    node.hdr.nkeys = n - 1;
    return extent_leaf_write(root, &node);
}

static int extent_leaf_insert(uint32_t root, uint64_t key, uint64_t value) {
    struct btree_node node;
    if (extent_leaf_read(root, &node) < 0) {
        return -1;
    }

    uint16_t n = node.hdr.nkeys;
    uint16_t i = 0;
    while (i < n && node.keys[i].key < key) {
        i++;
    }
    if (i < n && node.keys[i].key == key) {
        node.keys[i].value = value;
        return extent_leaf_write(root, &node);
    }
    if (n >= BTREE_ORDER) {
        return -1;
    }
    for (uint16_t j = n; j > i; j--) {
        node.keys[j] = node.keys[j - 1];
    }
    node.keys[i].key = key;
    node.keys[i].value = value;
    node.hdr.nkeys = n + 1;
    return extent_leaf_write(root, &node);
}

static uint64_t extent_pack(uint32_t len) {
    return (uint64_t)len;
}

static uint32_t extent_unpack(uint64_t v) {
    return (uint32_t)v;
}

static uint32_t extent_btree_checksum(const struct btree_node *node) {
    const uint8_t *p = (const uint8_t *)node;
    uint32_t csum_off = (uint32_t)((const uint8_t *)&node->hdr.checksum - p);
    uint32_t rsv_off = (uint32_t)((const uint8_t *)&node->hdr.reserved - p);
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < sizeof(*node); i++) {
        uint8_t v = p[i];
        if (i >= csum_off && i < csum_off + sizeof(node->hdr.checksum)) {
            v = 0;
        }
        if (i >= rsv_off && i < rsv_off + sizeof(node->hdr.reserved)) {
            v = 0;
        }
        hash ^= v;
        hash *= 16777619u;
    }
    return hash;
}

static int extent_btree_write_root(uint32_t root,
                                   const struct btree_key *keys,
                                   uint16_t nkeys) {
    if (root == 0 || root >= sb.nblocks || nkeys > BTREE_ORDER) {
        return -1;
    }

    struct btree_node node;
    memzero(&node, sizeof(node));
    node.hdr.magic = BTREE_MAGIC;
    node.hdr.type = BTREE_TYPE_NODE;
    node.hdr.logical = root;
    node.hdr.generation = sb.generation + 1;
    node.hdr.level = 0;
    node.hdr.nkeys = nkeys;
    for (uint16_t i = 0; i < nkeys; i++) {
        node.keys[i] = keys[i];
    }
    node.hdr.checksum = extent_btree_checksum(&node);

    struct buf *bp = bread(root);
    memmove(bp->data, &node, sizeof(node));
    bwrite(bp);
    brelse(bp);
    return 0;
}

static int block_is_free(uint32_t blockno) {
    uint32_t bmap_block = 1 + NSUPER + blockno / (BSIZE * 8);
    struct buf *bp = bread(bmap_block);
    uint32_t bi = blockno % (BSIZE * 8);
    uint32_t m = 1u << (bi % 8);
    int free = (bp->data[bi / 8] & m) == 0;
    brelse(bp);
    return free;
}

static int extent_tree_prev(uint32_t root, uint64_t start,
                            uint64_t *key_out, uint64_t *val_out) {
    if (root == 0) return -1;
    struct btree_node node;
    if (extent_leaf_read(root, &node) == 0) {
        uint64_t best_key = 0;
        uint64_t best_val = 0;
        int found = 0;
        for (uint16_t i = 0; i < node.hdr.nkeys; i++) {
            uint64_t k = node.keys[i].key;
            uint64_t v = node.keys[i].value;
            if (v == 0) continue;
            if (k <= start) {
                if (!found || k > best_key) {
                    best_key = k;
                    best_val = v;
                    found = 1;
                }
            }
        }
        if (!found) return -1;
        if (key_out) *key_out = best_key;
        if (val_out) *val_out = best_val;
        return 0;
    }

    uint64_t cursor = start;
    for (;;) {
        uint64_t key = 0;
        uint64_t val = 0;
        if (btree_lookup_le(root, cursor, &key, &val) < 0) {
            return -1;
        }
        if (val != 0) {
            if (key_out) *key_out = key;
            if (val_out) *val_out = val;
            return 0;
        }
        if (key == 0) {
            return -1;
        }
        cursor = key - 1;
    }
}

static int extent_tree_next(uint32_t root, uint64_t start,
                            uint64_t *key_out, uint64_t *val_out) {
    if (root == 0) return -1;
    struct btree_node node;
    if (extent_leaf_read(root, &node) == 0) {
        uint64_t best_key = 0;
        uint64_t best_val = 0;
        int found = 0;
        for (uint16_t i = 0; i < node.hdr.nkeys; i++) {
            uint64_t k = node.keys[i].key;
            uint64_t v = node.keys[i].value;
            if (v == 0) continue;
            if (k >= start) {
                if (!found || k < best_key) {
                    best_key = k;
                    best_val = v;
                    found = 1;
                }
            }
        }
        if (!found) return -1;
        if (key_out) *key_out = best_key;
        if (val_out) *val_out = best_val;
        return 0;
    }

    uint64_t cursor = start;
    for (;;) {
        uint64_t key = 0;
        uint64_t val = 0;
        if (btree_lookup_ge(root, cursor, &key, &val) < 0) {
            return -1;
        }
        if (val != 0) {
            if (key_out) *key_out = key;
            if (val_out) *val_out = val;
            return 0;
        }
        cursor = key + 1;
    }
}

static int extent_tree_add(uint32_t root, uint32_t start, uint32_t len,
                           uint32_t *out_root) {
    if (len == 0 || root == 0) return -1;
    if (start < sb.data_start) return -1;

    uint64_t new_start = start;
    uint64_t new_len = len;
    uint32_t new_root = root;
    int leaf_ok = 1;

    extent_meta_enter();

    uint64_t pk = 0;
    uint64_t pv = 0;
    if (extent_tree_prev(new_root, start, &pk, &pv) == 0) {
        uint32_t plen = extent_unpack(pv);
        if (pk + plen == start) {
            new_start = pk;
            new_len += plen;
            if (extent_leaf_remove(new_root, pk) < 0) {
                leaf_ok = 0;
            }
        }
    }

    for (;;) {
        uint64_t nk = 0;
        uint64_t nv = 0;
        uint64_t cursor = new_start + new_len;
        if (extent_tree_next(new_root, cursor, &nk, &nv) < 0) {
            break;
        }
        uint32_t nlen = extent_unpack(nv);
        if (nk != cursor) {
            break;
        }
        new_len += nlen;
        if (extent_leaf_remove(new_root, nk) < 0) {
            leaf_ok = 0;
            break;
        }
    }

    if (leaf_ok) {
        if (extent_leaf_insert(new_root, new_start,
                               extent_pack((uint32_t)new_len)) < 0) {
            leaf_ok = 0;
        }
    }

    if (out_root) {
        *out_root = new_root;
    }
    extent_meta_exit();
    if (leaf_ok) {
        return 0;
    }
    return -1;
}

static int extent_rebuild(uint32_t root, uint32_t *out_root) {
    struct btree_key keys[BTREE_ORDER];
    uint16_t nkeys = 0;
    uint32_t run_start = 0;
    uint32_t run_len = 0;

    for (uint32_t b = sb.data_start; b < sb.nblocks; b++) {
        if (block_is_free(b)) {
            if (run_len == 0) {
                run_start = b;
            }
            run_len++;
            continue;
        }

        if (run_len != 0) {
            if (nkeys < BTREE_ORDER) {
                keys[nkeys].key = run_start;
                keys[nkeys].value = extent_pack(run_len);
                nkeys++;
            }
            run_len = 0;
        }
    }

    if (run_len != 0 && nkeys < BTREE_ORDER) {
        keys[nkeys].key = run_start;
        keys[nkeys].value = extent_pack(run_len);
        nkeys++;
    }

    if (nkeys > 0 && nkeys <= BTREE_ORDER) {
        if (extent_btree_write_root(root, keys, nkeys) < 0) {
            return -1;
        }
        if (out_root) {
            *out_root = root;
        }
        return 0;
    }

    uint32_t new_root = root;
    run_len = 0;
    for (uint32_t b = sb.data_start; b < sb.nblocks; b++) {
        if (block_is_free(b)) {
            if (run_len == 0) {
                run_start = b;
            }
            run_len++;
            continue;
        }

        if (run_len != 0) {
            extent_meta_enter();
            if (btree_insert(new_root, run_start, extent_pack(run_len),
                             &new_root) < 0) {
                extent_meta_exit();
                return -1;
            }
            extent_meta_exit();
            run_len = 0;
        }
    }

    if (run_len != 0) {
        extent_meta_enter();
        if (btree_insert(new_root, run_start, extent_pack(run_len),
                         &new_root) < 0) {
            extent_meta_exit();
            return -1;
        }
        extent_meta_exit();
    }

    if (out_root) {
        *out_root = new_root;
    }
    return 0;
}

static int extent_root_update(uint32_t new_root) {
    if (sb.root_tree == 0) {
        sb.extent_root = new_root;
        return 0;
    }

    uint32_t root = sb.root_tree;
    if (btree_insert(root, ROOT_ITEM_EXTENT_ROOT, new_root, &root) < 0) {
        return -1;
    }
    sb.root_tree = root;
    sb.extent_root = new_root;
    return 0;
}

static int block_mark_alloc(uint32_t blockno) {
    uint32_t bmap_block = 1 + NSUPER + blockno / (BSIZE * 8);
    struct buf *bp = bread(bmap_block);
    uint32_t bi = blockno % (BSIZE * 8);
    uint32_t m = 1u << (bi % 8);
    if (bp->data[bi / 8] & m) {
        brelse(bp);
        return -1;
    }
    bp->data[bi / 8] |= m;
    bwrite(bp);
    brelse(bp);

    uint32_t refcnt_block = 1 + NSUPER + sb.nbitmap +
                            (blockno / REFCNTS_PER_BLOCK);
    bp = bread(refcnt_block);
    bp->data[blockno % REFCNTS_PER_BLOCK] = 1;
    bwrite(bp);
    brelse(bp);

    bp = bread(blockno);
    memzero(bp->data, BSIZE);
    bwrite(bp);
    brelse(bp);
    return 0;
}

void extent_init(void) {
    if (sb.extent_root != 0) {
        return;
    }

    uint32_t root = 0;
    if (btree_create_empty(0, &root) < 0) {
        kprintf("extent: init failed\n");
        return;
    }
    uint32_t new_root = 0;
    if (extent_rebuild(root, &new_root) < 0) {
        kprintf("extent: rebuild failed\n");
        return;
    }
    sb.extent_root = new_root;
    if (extent_root_update(new_root) < 0) {
        kprintf("extent: root tree update failed\n");
        return;
    }
    writesb();
}

static int extent_alloc_dir(uint32_t len, struct extent *out, int from_end) {
    if (sb.extent_root == 0) {
        extent_init();
    }
    if (len == 0) return -1;
    if (extent_meta) return -1;

    uint64_t k = 0;
    uint64_t v = 0;
    uint32_t start = 0;
    if (from_end) {
        uint64_t cursor = sb.nblocks - 1;
        for (;;) {
            if (btree_lookup_le(sb.extent_root, cursor, &k, &v) < 0) {
                return -1;
            }
            if (v == 0) {
                if (k == 0) return -1;
                cursor = k - 1;
                continue;
            }
            uint32_t avail = extent_unpack(v);
            uint32_t seg_start = (uint32_t)k;
            uint32_t seg_end = seg_start + avail - 1;
            if (seg_end < cursor) {
                cursor = seg_end;
            }
            if (avail < len) {
                if (seg_start == 0) return -1;
                cursor = seg_start - 1;
                continue;
            }
            start = seg_end - len + 1;
            if (start < seg_start) {
                if (seg_start == 0) return -1;
                cursor = seg_start - 1;
                continue;
            }
            break;
        }
    } else {
        uint64_t cursor = sb.data_start;
        for (;;) {
            if (btree_lookup_ge(sb.extent_root, cursor, &k, &v) < 0) {
                return -1;
            }
            if (v == 0) {
                cursor = k + 1;
                continue;
            }
            uint32_t avail = extent_unpack(v);
            if (avail >= len) {
                break;
            }
            cursor = k + 1;
        }
        start = (uint32_t)k;
    }

    for (uint32_t i = 0; i < len; i++) {
        if (block_mark_alloc(start + i) < 0) {
            for (uint32_t j = 0; j < i; j++) {
                bfree(start + j);
            }
            return -1;
        }
    }

    uint32_t new_root = sb.extent_root;
    if (extent_rebuild(sb.extent_root, &new_root) < 0) {
        for (uint32_t i = 0; i < len; i++) {
            bfree(start + i);
        }
        return -1;
    }
    if (new_root != sb.extent_root) {
        if (extent_root_update(new_root) < 0) {
            for (uint32_t i = 0; i < len; i++) {
                bfree(start + i);
            }
            return -1;
        }
    }
    writesb();

    if (out) {
        out->start = start;
        out->len = len;
    }
    return 0;
}

int extent_alloc(uint32_t len, struct extent *out) {
    return extent_alloc_dir(len, out, 0);
}

int extent_alloc_meta(uint32_t len, struct extent *out) {
    return extent_alloc_dir(len, out, 1);
}

int extent_reserve(uint32_t start, uint32_t len) {
    if (len == 0 || start < sb.data_start || start >= sb.nblocks) {
        return 0;
    }
    if (sb.extent_root == 0) {
        return 0;
    }
    if (extent_meta) {
        return 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (block_mark_alloc(start + i) < 0) {
            for (uint32_t j = 0; j < i; j++) {
                bfree(start + j);
            }
            return -1;
        }
    }

    uint32_t new_root = sb.extent_root;
    if (extent_rebuild(sb.extent_root, &new_root) < 0) {
        for (uint32_t i = 0; i < len; i++) {
            bfree(start + i);
        }
        return -1;
    }
    if (new_root != sb.extent_root) {
        if (extent_root_update(new_root) < 0) {
            for (uint32_t i = 0; i < len; i++) {
                bfree(start + i);
            }
            return -1;
        }
    }
    writesb();
    return 0;
}

void extent_free(uint32_t start, uint32_t len) {
    if (len == 0) return;
    if (deferred_n >= MAX_DEFERRED) {
        kprintf("extent: deferred list full\n");
        return;
    }
    deferred[deferred_n].start = start;
    deferred[deferred_n].len = len;
    deferred_n++;
}

int extent_commit(void) {
    if (sb.extent_root == 0) {
        extent_init();
    }
    if (sb.extent_root == 0) {
        return -1;
    }
    for (int i = 0; i < deferred_n; i++) {
        uint32_t start = deferred[i].start;
        uint32_t len = deferred[i].len;
        for (uint32_t b = 0; b < len; b++) {
            bfree(start + b);
        }
    }
    deferred_n = 0;

    uint32_t new_root = sb.extent_root;
    if (extent_rebuild(sb.extent_root, &new_root) < 0) {
        return -1;
    }
    if (new_root != sb.extent_root) {
        if (extent_root_update(new_root) < 0) {
            return -1;
        }
    }
    writesb();
    return 0;
}
