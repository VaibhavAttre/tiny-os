#include <kernel/fs_tree.h>
#include <kernel/btree.h>
#include <kernel/tree.h>
#include <kernel/extent.h>
#include <kernel/fs.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/buf.h>

static uint64_t fs_item_key(uint32_t ino, uint16_t type, uint32_t sub) {
    return ((uint64_t)ino << 32) |
           ((uint64_t)type << 28) |
           ((uint64_t)sub & 0x0fffffff);
}

static uint32_t fs_tree_next_ino = 0;

static uint16_t name_hash16(const char *name) {
    uint32_t h = 2166136261u;
    for (int i = 0; name[i]; i++) {
        h ^= (uint8_t)name[i];
        h *= 16777619u;
    }
    return (uint16_t)(h ^ (h >> 16));
}

static uint64_t dirent_key(uint32_t parent_ino, const char *name) {
    uint16_t h = name_hash16(name);
    return fs_item_key(parent_ino, FS_ITEM_DIRENT, h);
}

static uint64_t dirent_pack(uint32_t ino, uint32_t name_block) {
    return ((uint64_t)name_block << 32) | (uint64_t)ino;
}

static void dirent_unpack(uint64_t v, uint32_t *ino, uint32_t *name_block) {
    if (ino) *ino = (uint32_t)(v & 0xffffffffu);
    if (name_block) *name_block = (uint32_t)(v >> 32);
}

static uint64_t extent_key(uint32_t ino, uint64_t file_off) {
    uint64_t block = file_off / BSIZE;
    return fs_item_key(ino, FS_ITEM_EXTENT, (uint32_t)block);
}

static void extent_key_unpack(uint64_t key, uint32_t *ino, uint16_t *type,
                              uint32_t *block) {
    if (ino) *ino = (uint32_t)(key >> 32);
    if (type) *type = (uint16_t)((key >> 28) & 0xf);
    if (block) *block = (uint32_t)(key & 0x0fffffff);
}

static uint64_t extent_pack(uint32_t start, uint32_t len) {
    return ((uint64_t)start << 32) | (uint64_t)len;
}

static void extent_unpack(uint64_t v, uint32_t *start, uint32_t *len) {
    if (start) *start = (uint32_t)(v >> 32);
    if (len) *len = (uint32_t)(v & 0xffffffffu);
}

static uint64_t inode_pack(uint16_t type, uint64_t size) {
    if (size > 0x0000FFFFFFFFFFFFULL) {
        size = 0x0000FFFFFFFFFFFFULL;
    }
    return ((uint64_t)type << 48) | (size & 0x0000FFFFFFFFFFFFULL);
}

static void inode_unpack(uint64_t v, uint16_t *type, uint64_t *size) {
    uint16_t t = (uint16_t)(v >> 48);
    uint64_t s = v & 0x0000FFFFFFFFFFFFULL;
    if (t == 0) {
        t = T_FILE;  // Backward-compat with old size-only encoding.
    }
    if (type) *type = t;
    if (size) *size = s;
}

void fs_tree_init(void) {
    tree_init();
    if (sb.root_tree == 0) {
        kprintf("fs_tree: no root tree\n");
    }
    if (fs_tree_next_ino == 0) {
        fs_tree_next_ino = sb.fs_next_ino ? sb.fs_next_ino : 2;
    }
}

int fs_tree_set_inode(uint32_t ino, uint16_t type, uint64_t size) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }

    uint32_t new_root = 0;
    if (btree_insert((uint32_t)fs_root, fs_item_key(ino, FS_ITEM_INODE, 0),
                     inode_pack(type, size), &new_root) < 0) {
        return -1;
    }

    // Update root tree to point at new FS tree root.
    uint32_t root = sb.root_tree;
    if (btree_insert(root, ROOT_ITEM_FS_ROOT, new_root, &root) < 0) {
        return -1;
    }
    sb.root_tree = root;
    writesb();
    return 0;
}

int fs_tree_get_inode(uint32_t ino, uint16_t *type_out, uint64_t *size_out) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }
    uint64_t val = 0;
    if (btree_lookup((uint32_t)fs_root, fs_item_key(ino, FS_ITEM_INODE, 0),
                     &val) < 0) {
        return -1;
    }
    inode_unpack(val, type_out, size_out);
    return 0;
}

static int fs_tree_root_ensure(void) {
    uint16_t type = 0;
    uint64_t size = 0;
    if (fs_tree_get_inode(1, &type, &size) == 0) {
        if (type != T_DIR) {
            return fs_tree_set_inode(1, T_DIR, size);
        }
        return 0;
    }
    if (fs_tree_set_inode(1, T_DIR, 0) < 0) {
        return -1;
    }
    return 0;
}

static int fs_tree_walk(const char *path, uint32_t *parent_out, char *name_out) {
    if (!path || path[0] != '/') return -1;
    if (fs_tree_root_ensure() < 0) return -1;

    uint32_t cur = 1;
    const char *p = path;

    while (*p == '/') p++;
    if (*p == 0) {
        if (parent_out) *parent_out = 1;
        if (name_out) name_out[0] = 0;
        return 0;
    }

    char name[32];
    while (*p) {
        int len = 0;
        while (*p && *p != '/' && len < (int)sizeof(name) - 1) {
            name[len++] = *p++;
        }
        name[len] = 0;
        while (*p == '/') p++;

        if (*p == 0) {
            if (parent_out) *parent_out = cur;
            if (name_out) {
                memmove(name_out, name, len + 1);
            }
            return 0;
        }

        uint32_t next = 0;
        if (fs_tree_dir_lookup(cur, name, &next) < 0) {
            return -1;
        }
        cur = next;
    }

    return -1;
}

int fs_tree_lookup_path(const char *path, uint32_t *ino_out) {
    uint32_t parent = 0;
    char name[32];
    if (fs_tree_walk(path, &parent, name) < 0) return -1;
    if (name[0] == 0) {
        if (ino_out) *ino_out = parent;
        return 0;
    }
    return fs_tree_dir_lookup(parent, name, ino_out);
}

int fs_tree_create_file(const char *path, uint32_t *ino_out) {
    uint32_t parent = 0;
    char name[32];
    kprintf("fs_tree_create_file: path='%s'\n", path);
    if (fs_tree_walk(path, &parent, name) < 0) return -1;
    if (name[0] == 0) return -1;

    uint16_t parent_type = 0;
    if (fs_tree_get_inode(parent, &parent_type, 0) < 0 || parent_type != T_DIR) {
        return -1;
    }

    uint32_t ino = 0;
    if (fs_tree_dir_lookup(parent, name, &ino) == 0) {
        uint16_t type = 0;
        if (fs_tree_get_inode(ino, &type, 0) < 0) return -1;
        if (type != T_FILE) return -1;
        if (ino_out) *ino_out = ino;
        return 0;
    }

    ino = fs_tree_next_ino++;
    sb.fs_next_ino = fs_tree_next_ino;
    writesb();
    if (fs_tree_set_inode(ino, T_FILE, 0) < 0) return -1;
    if (fs_tree_dir_add(parent, name, ino) < 0) return -1;

    if (ino_out) *ino_out = ino;
    kprintf("fs_tree_create_file: ok ino=%u name='%s'\n", ino, name);
    return 0;
}

int fs_tree_create_dir(const char *path) {
    uint32_t parent = 0;
    char name[32];
    if (fs_tree_walk(path, &parent, name) < 0) return -1;
    if (name[0] == 0) return -1;

    uint16_t parent_type = 0;
    if (fs_tree_get_inode(parent, &parent_type, 0) < 0 || parent_type != T_DIR) {
        return -1;
    }

    uint32_t ino = 0;
    if (fs_tree_dir_lookup(parent, name, &ino) == 0) {
        uint16_t type = 0;
        if (fs_tree_get_inode(ino, &type, 0) < 0) return -1;
        return (type == T_DIR) ? 0 : -1;
    }

    ino = fs_tree_next_ino++;
    sb.fs_next_ino = fs_tree_next_ino;
    writesb();
    if (fs_tree_set_inode(ino, T_DIR, 0) < 0) return -1;
    if (fs_tree_dir_add(parent, name, ino) < 0) return -1;
    return 0;
}

static int fs_tree_dir_remove(uint32_t parent_ino, const char *name) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }

    uint64_t val = 0;
    if (btree_lookup((uint32_t)fs_root, dirent_key(parent_ino, name), &val) < 0) {
        return -1;
    }

    uint32_t name_block = 0;
    dirent_unpack(val, 0, &name_block);
    if (name_block) {
        extent_free(name_block, 1);
        if (extent_commit() < 0) {
            return -1;
        }
    }

    uint32_t new_root = 0;
    uint64_t key = dirent_key(parent_ino, name);
    if (btree_insert((uint32_t)fs_root, key, 0, &new_root) < 0) {
        return -1;
    }

    uint32_t root = sb.root_tree;
    if (btree_insert(root, ROOT_ITEM_FS_ROOT, new_root, &root) < 0) {
        return -1;
    }
    sb.root_tree = root;
    writesb();
    return 0;
}

int fs_tree_dir_add(uint32_t parent_ino, const char *name, uint32_t ino) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }

    struct extent ex;
    if (extent_alloc(1, &ex) < 0) {
        return -1;
    }
    struct buf *bp = bread(ex.start);
    memzero(bp->data, BSIZE);
    uint32_t i = 0;
    for (; i + 1 < BSIZE && name[i]; i++) {
        bp->data[i] = (uint8_t)name[i];
    }
    bp->data[i] = 0;
    bwrite(bp);
    brelse(bp);

    uint32_t new_root = 0;
    uint64_t key = dirent_key(parent_ino, name);
    if (btree_insert((uint32_t)fs_root, key, dirent_pack(ino, ex.start),
                     &new_root) < 0) {
        return -1;
    }

    uint32_t root = sb.root_tree;
    if (btree_insert(root, ROOT_ITEM_FS_ROOT, new_root, &root) < 0) {
        return -1;
    }
    sb.root_tree = root;
    writesb();
    return 0;
}

int fs_tree_dir_lookup(uint32_t parent_ino, const char *name, uint32_t *ino_out) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }
    uint64_t val = 0;
    if (btree_lookup((uint32_t)fs_root, dirent_key(parent_ino, name), &val) < 0) {
        return -1;
    }

    uint32_t ino = 0;
    uint32_t name_block = 0;
    dirent_unpack(val, &ino, &name_block);
    if (name_block == 0) return -1;

    struct buf *bp = bread(name_block);
    int match = (strncmp((const char *)bp->data, name, BSIZE) == 0);
    brelse(bp);
    if (!match) {
        return -1;
    }

    if (ino_out) {
        *ino_out = ino;
    }
    return 0;
}

static int fs_tree_dir_empty(uint32_t ino) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }

    uint64_t base = fs_item_key(ino, FS_ITEM_DIRENT, 0);
    uint64_t limit = fs_item_key(ino, FS_ITEM_DIRENT, 0x0fffffff);
    uint64_t cursor = base;

    for (;;) {
        uint64_t found_key = 0;
        uint64_t val = 0;
        if (btree_lookup_ge((uint32_t)fs_root, cursor, &found_key, &val) < 0) {
            return 1;
        }
        if (found_key > limit) {
            return 1;
        }
        cursor = found_key + 1;

        uint32_t key_ino = 0;
        uint16_t key_type = 0;
        uint32_t key_block = 0;
        extent_key_unpack(found_key, &key_ino, &key_type, &key_block);
        if (key_ino != ino || key_type != FS_ITEM_DIRENT) {
            continue;
        }
        if (val == 0) {
            continue;
        }
        return 0;
    }
}

static int fs_tree_drop_extents(uint32_t ino) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }

    uint64_t base = fs_item_key(ino, FS_ITEM_EXTENT, 0);
    uint64_t limit = fs_item_key(ino, FS_ITEM_EXTENT, 0x0fffffff);
    uint64_t cursor = base;
    uint32_t new_root = (uint32_t)fs_root;

    for (;;) {
        uint64_t found_key = 0;
        uint64_t val = 0;
        if (btree_lookup_ge(new_root, cursor, &found_key, &val) < 0) {
            break;
        }
        if (found_key > limit) {
            break;
        }
        cursor = found_key + 1;

        uint32_t key_ino = 0;
        uint16_t key_type = 0;
        uint32_t key_block = 0;
        extent_key_unpack(found_key, &key_ino, &key_type, &key_block);
        if (key_ino != ino || key_type != FS_ITEM_EXTENT) {
            continue;
        }
        if (val == 0) {
            continue;
        }

        uint32_t start = 0, len = 0;
        extent_unpack(val, &start, &len);
        extent_free(start, len);

        if (btree_insert(new_root, found_key, 0, &new_root) < 0) {
            return -1;
        }
    }

    uint32_t root = sb.root_tree;
    if (btree_insert(root, ROOT_ITEM_FS_ROOT, new_root, &root) < 0) {
        return -1;
    }
    sb.root_tree = root;
    writesb();
    return extent_commit();
}

int fs_tree_unlink_path(const char *path) {
    uint32_t parent = 0;
    char name[32];
    if (fs_tree_walk(path, &parent, name) < 0) return -1;
    if (name[0] == 0) return -1;

    uint32_t ino = 0;
    if (fs_tree_dir_lookup(parent, name, &ino) < 0) {
        return -1;
    }

    uint16_t type = 0;
    uint64_t size = 0;
    if (fs_tree_get_inode(ino, &type, &size) < 0) {
        return -1;
    }

    if (type == T_DIR) {
        int empty = fs_tree_dir_empty(ino);
        if (empty <= 0) {
            return -1;
        }
    }

    if (fs_tree_dir_remove(parent, name) < 0) {
        return -1;
    }

    if (type == T_FILE) {
        if (fs_tree_drop_extents(ino) < 0) {
            return -1;
        }
    }

    if (fs_tree_set_inode(ino, T_UNUSED, 0) < 0) {
        return -1;
    }

    return 0;
}

int fs_tree_rename_path(const char *oldpath, const char *newpath) {
    uint32_t old_parent = 0;
    char old_name[32];
    if (fs_tree_walk(oldpath, &old_parent, old_name) < 0) return -1;
    if (old_name[0] == 0) return -1;

    uint32_t new_parent = 0;
    char new_name[32];
    if (fs_tree_walk(newpath, &new_parent, new_name) < 0) return -1;
    if (new_name[0] == 0) return -1;

    if (old_parent != new_parent) {
        return -1;
    }

    uint32_t ino = 0;
    if (fs_tree_dir_lookup(old_parent, old_name, &ino) < 0) {
        return -1;
    }

    if (fs_tree_dir_add(new_parent, new_name, ino) < 0) {
        return -1;
    }

    if (fs_tree_dir_remove(old_parent, old_name) < 0) {
        return -1;
    }

    return 0;
}

int fs_tree_readdir(uint32_t parent_ino, uint64_t *cookie,
                    char *name_out, uint32_t name_len, uint32_t *ino_out) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }

    uint64_t base = fs_item_key(parent_ino, FS_ITEM_DIRENT, 0);
    uint64_t limit = fs_item_key(parent_ino, FS_ITEM_DIRENT, 0x0fffffff);
    uint64_t cursor = (cookie && *cookie > base) ? *cookie : base;

    for (;;) {
        uint64_t found_key = 0;
        uint64_t val = 0;
        if (btree_lookup_ge((uint32_t)fs_root, cursor, &found_key, &val) < 0) {
            return -1;
        }
        if (found_key > limit) {
            return -1;
        }
        cursor = found_key + 1;

        uint32_t key_ino = 0;
        uint16_t key_type = 0;
        uint32_t key_block = 0;
        extent_key_unpack(found_key, &key_ino, &key_type, &key_block);
        if (key_ino != parent_ino || key_type != FS_ITEM_DIRENT) {
            continue;
        }
        if (val == 0) {
            continue;
        }

        uint32_t ino = 0;
        uint32_t name_block = 0;
        dirent_unpack(val, &ino, &name_block);
        if (name_block == 0) {
            continue;
        }

        struct buf *bp = bread(name_block);
        if (name_out && name_len > 0) {
            uint32_t i;
            for (i = 0; i + 1 < name_len && i < BSIZE; i++) {
                name_out[i] = (char)bp->data[i];
                if (bp->data[i] == 0) break;
            }
            if (i + 1 >= name_len) {
                name_out[name_len - 1] = 0;
            }
        }
        brelse(bp);

        if (ino_out) *ino_out = ino;
        if (cookie) *cookie = cursor;
        return 0;
    }
}

int fs_tree_extent_add(uint32_t ino, uint64_t file_off, uint32_t start, uint32_t len) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }

    uint32_t new_root = 0;
    if (btree_insert((uint32_t)fs_root, extent_key(ino, file_off),
                     extent_pack(start, len), &new_root) < 0) {
        return -1;
    }

    uint32_t root = sb.root_tree;
    if (btree_insert(root, ROOT_ITEM_FS_ROOT, new_root, &root) < 0) {
        return -1;
    }
    sb.root_tree = root;
    writesb();
    return 0;
}

int fs_tree_extent_lookup(uint32_t ino, uint64_t file_off,
                          uint32_t *start_out, uint32_t *len_out) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }
    uint64_t val = 0;
    if (btree_lookup((uint32_t)fs_root, extent_key(ino, file_off), &val) < 0) {
        return -1;
    }
    extent_unpack(val, start_out, len_out);
    return 0;
}

static int fs_tree_extent_find(uint32_t ino, uint64_t file_off,
                               uint32_t *start_out, uint32_t *len_out,
                               uint64_t *ext_off_out) {
    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }

    uint64_t key = extent_key(ino, file_off);
    uint64_t found_key = 0;
    uint64_t val = 0;
    if (btree_lookup_le((uint32_t)fs_root, key, &found_key, &val) < 0) {
        return -1;
    }

    uint32_t key_ino = 0;
    uint16_t key_type = 0;
    uint32_t key_block = 0;
    extent_key_unpack(found_key, &key_ino, &key_type, &key_block);
    if (key_ino != ino || key_type != FS_ITEM_EXTENT) {
        return -1;
    }

    uint64_t ext_off = (uint64_t)key_block * (uint64_t)BSIZE;
    uint32_t start = 0, len = 0;
    extent_unpack(val, &start, &len);

    if (file_off < ext_off || file_off >= ext_off + (uint64_t)len * BSIZE) {
        return -1;
    }

    if (start_out) *start_out = start;
    if (len_out) *len_out = len;
    if (ext_off_out) *ext_off_out = ext_off;
    return 0;
}

int fs_tree_file_write(uint32_t ino, uint64_t off, const void *src, uint32_t n) {
    if (n == 0) return 0;

    uint16_t type = 0;
    uint64_t size = 0;
    if (fs_tree_get_inode(ino, &type, &size) < 0) {
        size = 0;
        type = T_FILE;
    }
    if (type != T_FILE) {
        return -1;
    }
    kprintf("fs_tree_file_write: ino=%u off=%u n=%u size=%u\n",
            ino, (unsigned)off, n, (unsigned)size);

    uint32_t start = 0, len = 0;
    uint64_t ext_off = 0;

    uint64_t pos = off;
    uint32_t remaining = n;
    const uint8_t *p = (const uint8_t *)src;

    while (remaining > 0) {
        if (fs_tree_extent_find(ino, pos, &start, &len, &ext_off) < 0) {
            struct extent ex;
            uint32_t blocks = (remaining + BSIZE - 1) / BSIZE;
            kprintf("fs_tree_file_write: alloc blocks=%u pos=%u\n",
                    blocks, (unsigned)pos);
            if (extent_alloc(blocks, &ex) < 0) {
                return -1;
            }
            if (fs_tree_extent_add(ino, pos, ex.start, ex.len) < 0) {
                return -1;
            }
            start = ex.start;
            len = ex.len;
            ext_off = pos - (pos % BSIZE);
        }
        uint64_t blk_index = (pos - ext_off) / BSIZE;
        if (blk_index >= len) {
            return -1;
        }
        uint32_t blockno = start + (uint32_t)blk_index;
        uint32_t boff = pos % BSIZE;
        uint32_t chunk = BSIZE - boff;
        if (chunk > remaining) chunk = remaining;

        struct buf *bp = bread(blockno);
        memmove(bp->data + boff, p, chunk);
        bwrite(bp);
        brelse(bp);

        remaining -= chunk;
        p += chunk;
        pos += chunk;
    }

    if (off + n > size) {
        fs_tree_set_inode(ino, type, off + n);
    }
    return (int)n;
}

int fs_tree_file_read(uint32_t ino, uint64_t off, void *dst, uint32_t n) {
    if (n == 0) return 0;

    uint16_t type = 0;
    uint64_t size = 0;
    if (fs_tree_get_inode(ino, &type, &size) < 0) {
        return -1;
    }
    if (type != T_FILE) {
        return -1;
    }
    if (off >= size) return 0;
    if (off + n > size) {
        n = (uint32_t)(size - off);
    }

    uint64_t pos = off;
    uint32_t remaining = n;
    uint8_t *p = (uint8_t *)dst;

    while (remaining > 0) {
        uint32_t start = 0, len = 0;
        uint64_t ext_off = 0;
        if (fs_tree_extent_find(ino, pos, &start, &len, &ext_off) < 0) {
            uint64_t fs_root = 0;
            uint64_t key = extent_key(ino, pos);
            uint64_t found_key = 0;
            uint64_t val = 0;
            uint32_t chunk = remaining;
            if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) == 0 &&
                btree_lookup_ge((uint32_t)fs_root, key, &found_key, &val) == 0) {
                uint32_t key_ino = 0;
                uint16_t key_type = 0;
                uint32_t key_block = 0;
                extent_key_unpack(found_key, &key_ino, &key_type, &key_block);
                if (key_ino == ino && key_type == FS_ITEM_EXTENT) {
                    uint64_t next_off = (uint64_t)key_block * (uint64_t)BSIZE;
                    if (next_off > pos) {
                        uint64_t gap = next_off - pos;
                        if (gap < chunk) {
                            chunk = (uint32_t)gap;
                        }
                    }
                }
            }
            memzero(p, chunk);
            remaining -= chunk;
            p += chunk;
            pos += chunk;
            continue;
        }

        uint64_t blk_index = (pos - ext_off) / BSIZE;
        if (blk_index >= len) {
            continue;
        }
        uint32_t blockno = start + (uint32_t)blk_index;
        uint32_t boff = pos % BSIZE;
        uint32_t chunk = BSIZE - boff;
        if (chunk > remaining) chunk = remaining;

        struct buf *bp = bread(blockno);
        memmove(p, bp->data + boff, chunk);
        brelse(bp);

        remaining -= chunk;
        p += chunk;
        pos += chunk;
    }

    return (int)n;
}
