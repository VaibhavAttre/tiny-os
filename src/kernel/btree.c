#include <kernel/btree.h>
#include <kernel/buf.h>
#include <kernel/fs.h>
#include <kernel/panic.h>
#include <kernel/string.h>

static uint32_t btree_checksum(const struct btree_node *node) {
    const uint8_t *p = (const uint8_t *)node;
    uint32_t csum_off = (uint32_t)((const uint8_t *)&node->hdr.checksum - p);
    uint32_t rsv_off = (uint32_t)((const uint8_t *)&node->hdr.reserved - p);
    uint32_t hash = 2166136261u; // FNV-1a
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

static int btree_read_node(uint32_t blockno, struct btree_node *out);
static int btree_write_node(uint32_t blockno, struct btree_node *node);

static int btree_node_validate(const struct btree_node *node, uint32_t blockno) {
    if (node->hdr.magic != BTREE_MAGIC) {
        return -1;
    }
    if (node->hdr.type != BTREE_TYPE_NODE) {
        return -1;
    }
    if (node->hdr.logical != 0 && node->hdr.logical != blockno) {
        return -1;
    }
    if (node->hdr.generation > sb.generation + 1) {
        return -1;
    }
    if (node->hdr.nkeys > BTREE_ORDER) {
        return -1;
    }
    if (node->hdr.level != 0 && node->hdr.level > 32) {
        return -1;
    }
    if (btree_checksum(node) != node->hdr.checksum) {
        return -1;
    }
    return 0;
}

int btree_lookup(uint32_t root_block, uint64_t key, uint64_t *out_value) {
    if (root_block == 0 || root_block >= sb.nblocks) {
        return -1;
    }

    uint32_t blk = root_block;

    for (;;) {
        struct btree_node node;
        if (btree_read_node(blk, &node) < 0) {
            return -1;
        }

        uint16_t n = node.hdr.nkeys;
        uint16_t i = 0;
        if (node.hdr.level == 0) {
            while (i < n && key > node.keys[i].key) {
                i++;
            }
            if (i < n && node.keys[i].key == key) {
                if (node.keys[i].value == 0) {
                    return -1;
                }
                if (out_value) {
                    *out_value = node.keys[i].value;
                }
                return 0;
            }
            return -1;
        }

        while (i < n && key >= node.keys[i].key) {
            i++;
        }

        uint64_t child = node.children[i];
        if (child == 0 || child >= sb.nblocks) {
            return -1;
        }
        blk = (uint32_t)child;
    }
}

static void btree_find_ge(uint32_t block, uint64_t key,
                          uint64_t *best_key, uint64_t *best_val, int *found) {
    struct btree_node node;
    if (btree_read_node(block, &node) < 0) return;

    if (node.hdr.level == 0) {
        for (uint16_t i = 0; i < node.hdr.nkeys; i++) {
            uint64_t k = node.keys[i].key;
            uint64_t v = node.keys[i].value;
            if (v == 0) continue;
            if (k >= key) {
                if (!*found || k < *best_key) {
                    *best_key = k;
                    *best_val = v;
                    *found = 1;
                }
            }
        }
        return;
    }

    for (uint16_t i = 0; i < node.hdr.nkeys + 1; i++) {
        if (node.children[i]) {
            btree_find_ge((uint32_t)node.children[i], key, best_key, best_val, found);
        }
    }
}

static void btree_find_le(uint32_t block, uint64_t key,
                          uint64_t *best_key, uint64_t *best_val, int *found) {
    struct btree_node node;
    if (btree_read_node(block, &node) < 0) return;

    if (node.hdr.level == 0) {
        for (uint16_t i = 0; i < node.hdr.nkeys; i++) {
            uint64_t k = node.keys[i].key;
            uint64_t v = node.keys[i].value;
            if (v == 0) continue;
            if (k <= key) {
                if (!*found || k > *best_key) {
                    *best_key = k;
                    *best_val = v;
                    *found = 1;
                }
            }
        }
        return;
    }

    for (uint16_t i = 0; i < node.hdr.nkeys + 1; i++) {
        if (node.children[i]) {
            btree_find_le((uint32_t)node.children[i], key, best_key, best_val, found);
        }
    }
}

int btree_lookup_ge(uint32_t root_block, uint64_t key,
                    uint64_t *out_key, uint64_t *out_value) {
    if (root_block == 0 || root_block >= sb.nblocks) {
        return -1;
    }
    uint64_t best_key = 0;
    uint64_t best_val = 0;
    int found = 0;
    btree_find_ge(root_block, key, &best_key, &best_val, &found);
    if (!found) return -1;
    if (out_key) *out_key = best_key;
    if (out_value) *out_value = best_val;
    return 0;
}

int btree_lookup_le(uint32_t root_block, uint64_t key,
                    uint64_t *out_key, uint64_t *out_value) {
    if (root_block == 0 || root_block >= sb.nblocks) {
        return -1;
    }
    uint64_t best_key = 0;
    uint64_t best_val = 0;
    int found = 0;
    btree_find_le(root_block, key, &best_key, &best_val, &found);
    if (!found) return -1;
    if (out_key) *out_key = best_key;
    if (out_value) *out_value = best_val;
    return 0;
}

static void btree_node_init(struct btree_node *node, uint16_t level) {
    memzero(node, sizeof(*node));
    node->hdr.magic = BTREE_MAGIC;
    node->hdr.type = BTREE_TYPE_NODE;
    node->hdr.level = level;
    node->hdr.nkeys = 0;
    node->hdr.generation = sb.generation + 1;
}

int btree_create_empty(uint16_t level, uint32_t *out_block) {
    if (out_block == 0) return -1;
    uint32_t blk = balloc();
    if (blk == 0) return -1;
    struct btree_node node;
    btree_node_init(&node, level);
    node.hdr.nkeys = 0;
    if (btree_write_node(blk, &node) < 0) return -1;
    *out_block = blk;
    return 0;
}

static int btree_write_node(uint32_t blockno, struct btree_node *node) {
    if (blockno == 0 || blockno >= sb.nblocks) {
        return -1;
    }
    node->hdr.type = BTREE_TYPE_NODE;
    node->hdr.logical = blockno;
    node->hdr.generation = sb.generation + 1;
    node->hdr.checksum = btree_checksum(node);
    struct buf *bp = bread(blockno);
    memmove(bp->data, node, sizeof(*node));
    bwrite(bp);
    brelse(bp);
    return 0;
}

static int btree_read_node(uint32_t blockno, struct btree_node *out) {
    if (blockno == 0 || blockno >= sb.nblocks) {
        return -1;
    }
    struct buf *bp = bread(blockno);
    memmove(out, bp->data, sizeof(*out));
    brelse(bp);
    return btree_node_validate(out, blockno);
}

struct btree_split {
    int split;
    uint64_t sep_key;
    uint32_t right_block;
};

static int btree_insert_leaf(const struct btree_node *old,
                             uint64_t key, uint64_t value,
                             uint32_t *out_block,
                             struct btree_split *out_split) {
    struct btree_key tmp[BTREE_ORDER + 1];
    uint16_t n = old->hdr.nkeys;
    uint16_t i = 0;
    int replaced = 0;

    while (i < n && key > old->keys[i].key) {
        tmp[i] = old->keys[i];
        i++;
    }

    if (i < n && key == old->keys[i].key) {
        tmp[i] = old->keys[i];
        tmp[i].value = value;
        replaced = 1;
        i++;
    } else {
        tmp[i].key = key;
        tmp[i].value = value;
    }

    for (uint16_t j = i; j < n; j++) {
        tmp[j + (replaced ? 0 : 1)] = old->keys[j];
    }

    uint16_t total = n + (replaced ? 0 : 1);
    if (total <= BTREE_ORDER) {
        uint32_t newblk = balloc();
        if (newblk == 0) return -1;
        struct btree_node node;
        btree_node_init(&node, 0);
        node.hdr.nkeys = total;
        for (uint16_t k = 0; k < total; k++) {
            node.keys[k] = tmp[k];
        }
        if (btree_write_node(newblk, &node) < 0) return -1;
        out_split->split = 0;
        *out_block = newblk;
        return 0;
    }

    uint16_t mid = total / 2;
    uint32_t leftblk = balloc();
    uint32_t rightblk = balloc();
    if (leftblk == 0 || rightblk == 0) return -1;

    struct btree_node left;
    struct btree_node right;
    btree_node_init(&left, 0);
    btree_node_init(&right, 0);

    left.hdr.nkeys = mid;
    for (uint16_t k = 0; k < mid; k++) {
        left.keys[k] = tmp[k];
    }

    right.hdr.nkeys = total - mid;
    for (uint16_t k = 0; k < right.hdr.nkeys; k++) {
        right.keys[k] = tmp[mid + k];
    }

    if (btree_write_node(leftblk, &left) < 0) return -1;
    if (btree_write_node(rightblk, &right) < 0) return -1;

    out_split->split = 1;
    out_split->sep_key = right.keys[0].key;
    out_split->right_block = rightblk;
    *out_block = leftblk;
    return 0;
}

static int btree_insert_internal(const struct btree_node *old,
                                 uint64_t key, uint64_t value,
                                 uint32_t *out_block,
                                 struct btree_split *out_split) {
    uint16_t n = old->hdr.nkeys;
    uint16_t i = 0;
    while (i < n && key >= old->keys[i].key) {
        i++;
    }

    uint32_t child = (uint32_t)old->children[i];
    struct btree_node child_node;
    if (btree_read_node(child, &child_node) < 0) {
        return -1;
    }

    struct btree_split child_split = {0};
    uint32_t new_child = 0;
    int r;
    if (child_node.hdr.level == 0) {
        r = btree_insert_leaf(&child_node, key, value, &new_child, &child_split);
    } else {
        r = btree_insert_internal(&child_node, key, value, &new_child, &child_split);
    }
    if (r < 0) return -1;

    struct btree_key keys[BTREE_ORDER + 1];
    uint64_t children[BTREE_ORDER + 2];
    uint16_t total = n;

    for (uint16_t j = 0; j < n; j++) {
        keys[j] = old->keys[j];
    }
    for (uint16_t j = 0; j < n + 1; j++) {
        children[j] = old->children[j];
    }

    children[i] = new_child;
    if (child_split.split) {
        for (uint16_t j = total; j > i; j--) {
            keys[j] = keys[j - 1];
        }
        keys[i].key = child_split.sep_key;
        keys[i].value = 0;
        for (uint16_t j = total + 1; j > i + 1; j--) {
            children[j] = children[j - 1];
        }
        children[i + 1] = child_split.right_block;
        total++;
    }

    if (total <= BTREE_ORDER) {
        uint32_t newblk = balloc();
        if (newblk == 0) return -1;
        struct btree_node node;
        btree_node_init(&node, old->hdr.level);
        node.hdr.nkeys = total;
        for (uint16_t k = 0; k < total; k++) {
            node.keys[k] = keys[k];
        }
        for (uint16_t k = 0; k < total + 1; k++) {
            node.children[k] = children[k];
        }
        if (btree_write_node(newblk, &node) < 0) return -1;
        out_split->split = 0;
        *out_block = newblk;
        return 0;
    }

    uint16_t mid = total / 2;
    uint32_t leftblk = balloc();
    uint32_t rightblk = balloc();
    if (leftblk == 0 || rightblk == 0) return -1;

    struct btree_node left;
    struct btree_node right;
    btree_node_init(&left, old->hdr.level);
    btree_node_init(&right, old->hdr.level);

    left.hdr.nkeys = mid;
    for (uint16_t k = 0; k < mid; k++) {
        left.keys[k] = keys[k];
    }
    for (uint16_t k = 0; k < mid + 1; k++) {
        left.children[k] = children[k];
    }

    right.hdr.nkeys = total - mid - 1;
    for (uint16_t k = 0; k < right.hdr.nkeys; k++) {
        right.keys[k] = keys[mid + 1 + k];
    }
    for (uint16_t k = 0; k < right.hdr.nkeys + 1; k++) {
        right.children[k] = children[mid + 1 + k];
    }

    if (btree_write_node(leftblk, &left) < 0) return -1;
    if (btree_write_node(rightblk, &right) < 0) return -1;

    out_split->split = 1;
    out_split->sep_key = keys[mid].key;
    out_split->right_block = rightblk;
    *out_block = leftblk;
    return 0;
}

int btree_insert(uint32_t root_block, uint64_t key, uint64_t value,
                 uint32_t *new_root_block) {
    if (new_root_block == 0) return -1;

    if (root_block == 0) {
        uint32_t newblk = balloc();
        if (newblk == 0) return -1;
        struct btree_node leaf;
        btree_node_init(&leaf, 0);
        leaf.hdr.nkeys = 1;
        leaf.keys[0].key = key;
        leaf.keys[0].value = value;
        if (btree_write_node(newblk, &leaf) < 0) return -1;
        *new_root_block = newblk;
        return 0;
    }

    struct btree_node root;
    if (btree_read_node(root_block, &root) < 0) {
        return -1;
    }

    struct btree_split split = {0};
    uint32_t new_root = 0;
    int r;
    if (root.hdr.level == 0) {
        r = btree_insert_leaf(&root, key, value, &new_root, &split);
    } else {
        r = btree_insert_internal(&root, key, value, &new_root, &split);
    }
    if (r < 0) return -1;

    if (!split.split) {
        *new_root_block = new_root;
        return 0;
    }

    uint32_t topblk = balloc();
    if (topblk == 0) return -1;
    struct btree_node top;
    btree_node_init(&top, root.hdr.level + 1);
    top.hdr.nkeys = 1;
    top.keys[0].key = split.sep_key;
    top.keys[0].value = 0;
    top.children[0] = new_root;
    top.children[1] = split.right_block;

    if (btree_write_node(topblk, &top) < 0) return -1;
    *new_root_block = topblk;
    return 0;
}

int btree_commit_root(uint32_t new_root_block) {
    if (new_root_block == 0 || new_root_block >= sb.nblocks) {
        return -1;
    }
    sb.btree_root = new_root_block;
    writesb();
    return 0;
}

void btree_txn_begin(struct btree_txn *txn) {
    if (!txn) return;
    txn->root = sb.btree_root;
    txn->new_root = sb.btree_root;
}

int btree_txn_insert(struct btree_txn *txn, uint64_t key, uint64_t value) {
    if (!txn) return -1;
    uint32_t out = 0;
    if (btree_insert(txn->new_root, key, value, &out) < 0) {
        return -1;
    }
    txn->new_root = out;
    return 0;
}

int btree_txn_commit(struct btree_txn *txn) {
    if (!txn) return -1;
    if (txn->new_root == 0) {
        return -1;
    }
    if (txn->new_root == txn->root) {
        return 0;
    }
    return btree_commit_root(txn->new_root);
}
