#pragma once
#include <stdint.h>

#define ROOT_ITEM_EXTENT_ROOT 1
#define ROOT_ITEM_FS_ROOT 2
#define ROOT_ITEM_SUBVOL_NEXT 3
#define ROOT_ITEM_EXTENT_REF_ROOT 4
#define ROOT_ITEM_SUBVOL_BASE 0x1000

void tree_init(void);
int tree_root_get(uint64_t item_type, uint64_t *out_block);
int tree_subvol_create(uint64_t *id_out);
int tree_subvol_get(uint64_t id, uint64_t *root_out);
int tree_subvol_set_current(uint64_t id);
uint64_t tree_subvol_current(void);
