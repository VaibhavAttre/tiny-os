#pragma once
#include <stdint.h>
#include <kernel/buf.h>

//
// CoW Filesystem On-Disk Layout
//
// Block 0:             Boot block (reserved)
// Block 1..NSUPER:     Superblocks (redundant copies)
// Block NSUPER+1..N:   Block bitmap (1 bit per block)
// Block N+1..M:        Refcount blocks (1 byte per block)
// Block M+1..K:        Inode blocks
// Block K+1..end:      Data blocks
//

#define FS_MAGIC    0x434F5746  // "COWF" - CoW Filesystem

// Number of superblock copies.
#define NSUPER     2

// Filesystem parameters
#define ROOTINO     1           // Root directory inode number
#define NDIRECT     12          // Direct block pointers per inode
#define NINDIRECT   (BSIZE / sizeof(uint32_t))  // Indirect block entries
#define MAXFILE     (NDIRECT + NINDIRECT)       // Max blocks per file

// Inode types
#define T_UNUSED    0
#define T_DIR       1
#define T_FILE      2

// On-disk superblock (block 1)
struct superblock {
    uint32_t magic;         // FS_MAGIC
    uint32_t version;       // Filesystem version
    uint32_t nblocks;       // Total blocks on disk
    uint32_t ninodes;       // Number of inodes
    uint32_t nbitmap;       // Number of bitmap blocks
    uint32_t nrefcnt;       // Number of refcount blocks (for CoW)
    uint32_t inode_start;   // First inode block
    uint32_t data_start;    // First data block
    uint32_t root_ino;      // Root directory inode
    uint32_t btree_root;    // Root block for experimental B-tree metadata
    uint32_t extent_root;   // Root block for free-space extents
    uint32_t root_tree;     // Root tree for metadata trees
    uint32_t fs_next_ino;   // Next FS-tree inode number
    uint64_t generation;    // Superblock generation
    uint32_t checksum;      // Checksum of superblock (checksum field zeroed)
    uint32_t reserved;      // Padding/reserved
};

// Block refcounts for CoW (1 byte per block, 1024 refcounts per block)
#define REFCNTS_PER_BLOCK (BSIZE / sizeof(uint8_t))

// On-disk inode (64 bytes, 16 per block)
#define INODES_PER_BLOCK (BSIZE / sizeof(struct dinode))

struct dinode {
    uint16_t type; // File type (T_FILE, T_DIR, etc)
    uint16_t nlink; // Number of hard links
    uint32_t size;  // Size in bytes
    uint32_t refcnt;   // Reference count (for CoW)
    uint32_t addrs[NDIRECT+1]; // Data block addresses (last is indirect)
};

// Directory entry (32 bytes, 32 per block)
#define DIRENT_NAMELEN  28
#define DIRENTS_PER_BLOCK (BSIZE / sizeof(struct dirent))

struct dirent {
    uint32_t inum;    // Inode number (0 = unused)
    char name[DIRENT_NAMELEN];
};

// In-memory inode (cached)
struct inode {
    uint32_t inum;          // Inode number
    int refcnt;             // In-memory reference count
    int valid;              // Has been read from disk?
    
    // Copy of on-disk inode
    uint16_t type;
    uint16_t nlink;
    uint32_t size;
    uint32_t disk_refcnt;   // On-disk refcount (for CoW)
    uint32_t addrs[NDIRECT+1];
};

// Global superblock (cached in memory)
extern struct superblock sb;

// Filesystem API
void fsinit(void);
void readsb(void);
void writesb(void);

// Block allocation
uint32_t        balloc(void);
void bfree(uint32_t blockno);

// Inode operations  
struct inode*   iget(uint32_t inum);
struct inode*   idup(struct inode *ip);
void iput(struct inode *ip);
void ilock(struct inode *ip);
void iunlock(struct inode *ip);
struct inode*   ialloc(uint16_t type);
void iupdate(struct inode *ip);
void itrunc(struct inode *ip);
void itrunc_to(struct inode *ip, uint32_t newsize);
int readi(struct inode *ip, void *dst, uint32_t off, uint32_t n);
int writei(struct inode *ip, void *src, uint32_t off, uint32_t n);

// Directory operations
struct inode*   dirlookup(struct inode *dp, char *name, uint32_t *poff);
int dirlink(struct inode *dp, char *name, uint32_t inum);

// Path resolution
struct inode*   namei(char *path);
struct inode*   nameiparent(char *path, char *name);

// High-level file operations
struct inode*   create(char *path, uint16_t type);

// CoW operations
uint8_t brefcnt_get(uint32_t blockno);
void brefcnt_inc(uint32_t blockno);
void brefcnt_dec(uint32_t blockno);
struct inode*   iclone(struct inode *src);  // Clone/reflink an inode
