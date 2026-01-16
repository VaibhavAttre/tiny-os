#pragma once
#include <stdint.h>

// Root tree item types.
#define ROOT_ITEM_EXTENT_ROOT 1
#define ROOT_ITEM_FS_ROOT     2

void tree_init(void);
int tree_root_get(uint64_t item_type, uint64_t *out_block);
