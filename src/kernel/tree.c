#include <kernel/tree.h>
#include <kernel/btree.h>
#include <kernel/extent.h>
#include <kernel/fs.h>
#include <kernel/printf.h>

static uint64_t root_item_key(uint64_t item_type) {
    return item_type;
}

static uint64_t subvol_key(uint64_t id) {
    return ROOT_ITEM_SUBVOL_BASE + id;
}

static uint64_t current_subvol = 1;

void tree_init(void) {
    if (sb.root_tree != 0) {
        return;
    }

    extent_init();
    if (sb.extent_root == 0) {
        kprintf("tree: no extent root\n");
        return;
    }

    uint32_t fs_root = 0;
    if (btree_create_empty(0, &fs_root) < 0) {
        kprintf("tree: fs root create failed\n");
        return;
    }

    uint32_t ref_root = 0;
    if (btree_create_empty(0, &ref_root) < 0) {
        kprintf("tree: extent ref root create failed\n");
        return;
    }

    uint32_t root = 0;
    if (btree_insert(root, root_item_key(ROOT_ITEM_EXTENT_ROOT),
                     sb.extent_root, &root) < 0 ||
        btree_insert(root, root_item_key(ROOT_ITEM_FS_ROOT),
                     fs_root, &root) < 0 ||
        btree_insert(root, root_item_key(ROOT_ITEM_EXTENT_REF_ROOT),
                     ref_root, &root) < 0 ||
        btree_insert(root, root_item_key(ROOT_ITEM_SUBVOL_NEXT),
                     2, &root) < 0 ||
        btree_insert(root, subvol_key(1), fs_root, &root) < 0) {
        kprintf("tree: root tree insert failed\n");
        return;
    }

    sb.root_tree = root;
    writesb();
    current_subvol = 1;
}

int tree_root_get(uint64_t item_type, uint64_t *out_block) {
    if (sb.root_tree == 0) {
        return -1;
    }
    if (item_type == ROOT_ITEM_FS_ROOT) {
        uint64_t root = 0;
        if (tree_subvol_get(current_subvol ? current_subvol : 1, &root) < 0) {
            return -1;
        }
        if (out_block) *out_block = root;
        return 0;
    }
    return btree_lookup(sb.root_tree, root_item_key(item_type), out_block);
}

int tree_subvol_get(uint64_t id, uint64_t *root_out) {
    if (sb.root_tree == 0) {
        return -1;
    }
    return btree_lookup(sb.root_tree, subvol_key(id), root_out);
}

int tree_subvol_create(uint64_t *id_out) {
    if (sb.root_tree == 0) {
        return -1;
    }

    uint64_t next = 0;
    if (btree_lookup(sb.root_tree, root_item_key(ROOT_ITEM_SUBVOL_NEXT),
                     &next) < 0) {
        next = 2;
    }
    if (next == 0) next = 2;

    uint64_t fs_root = 0;
    if (tree_root_get(ROOT_ITEM_FS_ROOT, &fs_root) < 0) {
        return -1;
    }

    uint32_t root = sb.root_tree;
    if (btree_insert(root, subvol_key(next), fs_root, &root) < 0) {
        return -1;
    }
    if (btree_insert(root, root_item_key(ROOT_ITEM_SUBVOL_NEXT),
                     next + 1, &root) < 0) {
        return -1;
    }

    sb.root_tree = root;
    writesb();
    if (id_out) *id_out = next;
    return 0;
}

int tree_subvol_set_current(uint64_t id) {
    if (sb.root_tree == 0) {
        return -1;
    }

    uint64_t fs_root = 0;
    if (tree_subvol_get(id, &fs_root) < 0) {
        return -1;
    }
    current_subvol = id;
    return 0;
}

uint64_t tree_subvol_current(void) {
    return current_subvol ? current_subvol : 1;
}
