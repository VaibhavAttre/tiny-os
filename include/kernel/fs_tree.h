#pragma once
#include <stdint.h>

// Minimal FS tree items (prototype).
#define FS_ITEM_INODE  1
#define FS_ITEM_DIRENT 2
#define FS_ITEM_EXTENT 3

void fs_tree_init(void);
int fs_tree_set_inode(uint32_t ino, uint16_t type, uint64_t size);
int fs_tree_get_inode(uint32_t ino, uint16_t *type_out, uint64_t *size_out);
int fs_tree_dir_add(uint32_t parent_ino, const char *name, uint32_t ino);
int fs_tree_dir_lookup(uint32_t parent_ino, const char *name, uint32_t *ino_out);
int fs_tree_extent_add(uint32_t ino, uint64_t file_off, uint32_t start, uint32_t len);
int fs_tree_extent_lookup(uint32_t ino, uint64_t file_off,
                          uint32_t *start_out, uint32_t *len_out);
int fs_tree_file_write(uint32_t ino, uint64_t off, const void *src, uint32_t n);
int fs_tree_file_read(uint32_t ino, uint64_t off, void *dst, uint32_t n);
int fs_tree_create_file(const char *path, uint32_t *ino_out);
int fs_tree_create_dir(const char *path);
int fs_tree_lookup_path(const char *path, uint32_t *ino_out);
int fs_tree_unlink_path(const char *path);
int fs_tree_rename_path(const char *oldpath, const char *newpath);
int fs_tree_readdir(uint32_t parent_ino, uint64_t *cookie,
                    char *name_out, uint32_t name_len, uint32_t *ino_out);
