#pragma once
#include <stdint.h>

// Minimal on-disk B-tree format (read-only traversal for now).
// Stored inside filesystem blocks (BSIZE).

#define BTREE_MAGIC 0x42545245  // "BTRE"
#define BTREE_ORDER 8           // Max keys per node (keeps stack usage low)

enum {
    BTREE_NODE_LEAF = 1,
    BTREE_NODE_INTERNAL = 2,
};

struct btree_hdr {
    uint32_t magic;
    uint16_t level;       // 0 = leaf
    uint16_t nkeys;
    uint64_t generation;
    uint32_t checksum;
    uint32_t reserved;
};

struct btree_key {
    uint64_t key;
    uint64_t value;
};

struct btree_node {
    struct btree_hdr hdr;
    uint64_t children[BTREE_ORDER + 1];  // internal nodes only
    struct btree_key keys[BTREE_ORDER];  // internal + leaf
};

// Read-only lookup.
// Returns 0 on success, -1 on failure/not found.
int btree_lookup(uint32_t root_block, uint64_t key, uint64_t *out_value);

// Range-like helpers (linear scan; returns first >= or last <=).
int btree_lookup_ge(uint32_t root_block, uint64_t key,
                    uint64_t *out_key, uint64_t *out_value);
int btree_lookup_le(uint32_t root_block, uint64_t key,
                    uint64_t *out_key, uint64_t *out_value);

// CoW insert/update.
// Creates a new root and leaves the old tree intact.
// Returns 0 on success, -1 on failure.
int btree_insert(uint32_t root_block, uint64_t key, uint64_t value,
                 uint32_t *new_root_block);

// Commit a new B-tree root to the superblock (minimal transaction commit).
// Returns 0 on success, -1 on failure.
int btree_commit_root(uint32_t new_root_block);

// Allocate an empty B-tree root node at the given level.
int btree_create_empty(uint16_t level, uint32_t *out_block);

// Minimal transaction wrapper (single-writer, no concurrency).
struct btree_txn {
    uint32_t root;
    uint32_t new_root;
};

void btree_txn_begin(struct btree_txn *txn);
int btree_txn_insert(struct btree_txn *txn, uint64_t key, uint64_t value);
int btree_txn_commit(struct btree_txn *txn);
