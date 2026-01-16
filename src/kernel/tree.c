#include <kernel/tree.h>
#include <kernel/btree.h>
#include <kernel/extent.h>
#include <kernel/fs.h>
#include <kernel/printf.h>

static uint64_t root_item_key(uint64_t item_type) {
    return item_type;
}

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

    uint32_t root = 0;
    if (btree_insert(root, root_item_key(ROOT_ITEM_EXTENT_ROOT),
                     sb.extent_root, &root) < 0 ||
        btree_insert(root, root_item_key(ROOT_ITEM_FS_ROOT),
                     fs_root, &root) < 0) {
        kprintf("tree: root tree insert failed\n");
        return;
    }

    sb.root_tree = root;
    writesb();
}

int tree_root_get(uint64_t item_type, uint64_t *out_block) {
    if (sb.root_tree == 0) {
        return -1;
    }
    return btree_lookup(sb.root_tree, root_item_key(item_type), out_block);
}
