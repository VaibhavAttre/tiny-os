//
// CoW Filesystem Implementation
//

#include <kernel/fs.h>
#include <kernel/buf.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>

// Cached superblock
struct superblock sb;

// Read superblock from disk
void readsb(void) {
    struct buf *bp = bread(1);  // Superblock is at block 1
    memmove(&sb, bp->data, sizeof(sb));
    brelse(bp);
}

// Initialize filesystem
void fsinit(void) {
    readsb();
    
    if (sb.magic != FS_MAGIC) {
        kprintf("fs: no valid filesystem found (magic=%x)\n", sb.magic);
        kprintf("fs: run mkfs to format the disk\n");
        return;
    }
    
    kprintf("fs: mounted (v%d, %d blocks, %d inodes)\n", 
            sb.version, sb.nblocks, sb.ninodes);
}

//
// Block Reference Counting (for CoW)
//

// Get refcount for a block
uint8_t brefcnt_get(uint32_t blockno) {
    if (blockno < sb.data_start || blockno >= sb.nblocks) {
        return 0;
    }
    
    uint32_t refcnt_block = 2 + sb.nbitmap + (blockno / REFCNTS_PER_BLOCK);
    struct buf *bp = bread(refcnt_block);
    uint8_t refcnt = bp->data[blockno % REFCNTS_PER_BLOCK];
    brelse(bp);
    return refcnt;
}

// Increment refcount for a block
void brefcnt_inc(uint32_t blockno) {
    if (blockno < sb.data_start || blockno >= sb.nblocks) {
        return;
    }
    
    uint32_t refcnt_block = 2 + sb.nbitmap + (blockno / REFCNTS_PER_BLOCK);
    struct buf *bp = bread(refcnt_block);
    uint32_t idx = blockno % REFCNTS_PER_BLOCK;
    
    if (bp->data[idx] < 255) {
        bp->data[idx]++;
        bwrite(bp);
    }
    brelse(bp);
}

// Decrement refcount for a block, free if reaches 0
void brefcnt_dec(uint32_t blockno) {
    if (blockno < sb.data_start || blockno >= sb.nblocks) {
        return;
    }
    
    uint32_t refcnt_block = 2 + sb.nbitmap + (blockno / REFCNTS_PER_BLOCK);
    struct buf *bp = bread(refcnt_block);
    uint32_t idx = blockno % REFCNTS_PER_BLOCK;
    
    if (bp->data[idx] > 0) {
        bp->data[idx]--;
        bwrite(bp);
        
        if (bp->data[idx] == 0) {
            brelse(bp);
            // Actually free the block in bitmap
            uint32_t bmap_block = 2 + blockno / (BSIZE * 8);
            bp = bread(bmap_block);
            uint32_t bi = blockno % (BSIZE * 8);
            bp->data[bi / 8] &= ~(1 << (bi % 8));
            bwrite(bp);
        }
    }
    brelse(bp);
}

//
// Block Allocation
//

// Allocate a zeroed disk block.
// Returns block number, or 0 if out of space.
uint32_t balloc(void) {
    struct buf *bp;
    
    // Scan through bitmap blocks
    for (uint32_t b = 0; b < sb.nblocks; b += BSIZE * 8) {
        uint32_t bmap_block = 2 + b / (BSIZE * 8);
        bp = bread(bmap_block);
        
        // Check each bit in this bitmap block
        for (uint32_t bi = 0; bi < BSIZE * 8 && b + bi < sb.nblocks; bi++) {
            uint32_t m = 1 << (bi % 8);
            
            if ((bp->data[bi / 8] & m) == 0) {
                // Found free block
                bp->data[bi / 8] |= m;
                bwrite(bp);
                brelse(bp);
                
                uint32_t blockno = b + bi;
                
                // Set refcount to 1
                uint32_t refcnt_block = 2 + sb.nbitmap + (blockno / REFCNTS_PER_BLOCK);
                bp = bread(refcnt_block);
                bp->data[blockno % REFCNTS_PER_BLOCK] = 1;
                bwrite(bp);
                brelse(bp);
                
                // Zero the block
                bp = bread(blockno);
                memzero(bp->data, BSIZE);
                bwrite(bp);
                brelse(bp);
                
                return blockno;
            }
        }
        brelse(bp);
    }
    
    kprintf("fs: out of disk space\n");
    return 0;
}

// Free a disk block (decrements refcount, actually frees when 0)
void bfree(uint32_t blockno) {
    if (blockno == 0) return;
    if (blockno >= sb.nblocks) {
        panic("bfree: block out of range");
    }
    
    brefcnt_dec(blockno);
}

//
// Inode Cache
//

#define NINODE 50  // Maximum number of cached inodes

static struct {
    struct inode inode[NINODE];
} icache;

// Find inode in cache, or allocate a slot.
// Increments reference count.
struct inode* iget(uint32_t inum) {
    struct inode *ip, *empty = 0;
    
    // Check if already cached
    for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {
        if (ip->refcnt > 0 && ip->inum == inum) {
            ip->refcnt++;
            return ip;
        }
        if (empty == 0 && ip->refcnt == 0) {
            empty = ip;
        }
    }
    
    // Not found - use empty slot
    if (empty == 0) {
        panic("iget: no inodes available");
    }
    
    ip = empty;
    ip->inum = inum;
    ip->refcnt = 1;
    ip->valid = 0;
    
    return ip;
}

// Increment reference count for inode
struct inode* idup(struct inode *ip) {
    if (ip) {
        ip->refcnt++;
    }
    return ip;
}

// Drop a reference to an inode.
// If this was the last reference, the inode can be reused.
void iput(struct inode *ip) {
    if (!ip) return;
    if (ip->refcnt < 1) {
        panic("iput: refcnt < 1");
    }
    ip->refcnt--;
}

// Read inode from disk if not already valid.
void ilock(struct inode *ip) {
    if (ip == 0 || ip->refcnt < 1) {
        panic("ilock");
    }
    
    if (ip->valid == 0) {
        // Read from disk
        uint32_t block = sb.inode_start + ip->inum / INODES_PER_BLOCK;
        uint32_t offset = (ip->inum % INODES_PER_BLOCK) * sizeof(struct dinode);
        
        struct buf *bp = bread(block);
        struct dinode *dip = (struct dinode *)(bp->data + offset);
        
        ip->type = dip->type;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        ip->disk_refcnt = dip->refcnt;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        
        brelse(bp);
        ip->valid = 1;
    }
}

// Release lock (currently a no-op, will add real locking later)
void iunlock(struct inode *ip) {
    if (ip == 0 || ip->refcnt < 1) {
        panic("iunlock");
    }
}

// Write inode to disk.
void iupdate(struct inode *ip) {
    uint32_t block = sb.inode_start + ip->inum / INODES_PER_BLOCK;
    uint32_t offset = (ip->inum % INODES_PER_BLOCK) * sizeof(struct dinode);
    
    struct buf *bp = bread(block);
    struct dinode *dip = (struct dinode *)(bp->data + offset);
    
    dip->type = ip->type;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    dip->refcnt = ip->disk_refcnt;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    
    bwrite(bp);
    brelse(bp);
}

// Allocate an inode on disk.
struct inode* ialloc(uint16_t type) {
    for (uint32_t inum = 1; inum < sb.ninodes; inum++) {
        uint32_t block = sb.inode_start + inum / INODES_PER_BLOCK;
        uint32_t offset = (inum % INODES_PER_BLOCK) * sizeof(struct dinode);
        
        struct buf *bp = bread(block);
        struct dinode *dip = (struct dinode *)(bp->data + offset);
        
        if (dip->type == T_UNUSED) {
            // Found free inode
            memzero(dip, sizeof(*dip));
            dip->type = type;
            dip->nlink = 1;
            dip->refcnt = 1;
            bwrite(bp);
            brelse(bp);
            
            return iget(inum);
        }
        brelse(bp);
    }
    
    kprintf("fs: no free inodes\n");
    return 0;
}

//
// Block mapping for inodes
//

// Return the disk block address of the nth block in inode ip.
// If there is no such block, allocate one.
// Copy a block for CoW
static uint32_t bcopy_cow(uint32_t oldblock) {
    uint32_t newblock = balloc();
    if (newblock == 0) return 0;
    
    struct buf *old_bp = bread(oldblock);
    struct buf *new_bp = bread(newblock);
    memmove(new_bp->data, old_bp->data, BSIZE);
    bwrite(new_bp);
    brelse(new_bp);
    brelse(old_bp);
    
    // Decrement old block's refcount
    brefcnt_dec(oldblock);
    
    return newblock;
}

// Get block address for inode, with optional CoW for writes
static uint32_t bmap_internal(struct inode *ip, uint32_t bn, int forwrite) {
    uint32_t addr;
    struct buf *bp;
    
    if (bn < NDIRECT) {
        if ((addr = ip->addrs[bn]) == 0) {
            addr = balloc();
            if (addr == 0) return 0;
            ip->addrs[bn] = addr;
        } else if (forwrite && brefcnt_get(addr) > 1) {
            // CoW: block is shared, copy it
            addr = bcopy_cow(addr);
            if (addr == 0) return 0;
            ip->addrs[bn] = addr;
            iupdate(ip);
        }
        return addr;
    }
    
    bn -= NDIRECT;
    
    if (bn < NINDIRECT) {
        // Load indirect block
        if ((addr = ip->addrs[NDIRECT]) == 0) {
            addr = balloc();
            if (addr == 0) return 0;
            ip->addrs[NDIRECT] = addr;
        } else if (forwrite && brefcnt_get(addr) > 1) {
            // CoW for indirect block itself
            addr = bcopy_cow(addr);
            if (addr == 0) return 0;
            ip->addrs[NDIRECT] = addr;
            iupdate(ip);
        }
        
        bp = bread(addr);
        uint32_t *a = (uint32_t *)bp->data;
        
        if ((addr = a[bn]) == 0) {
            addr = balloc();
            if (addr) {
                a[bn] = addr;
                bwrite(bp);
            }
        } else if (forwrite && brefcnt_get(addr) > 1) {
            // CoW for data block
            addr = bcopy_cow(addr);
            if (addr) {
                a[bn] = addr;
                bwrite(bp);
            }
        }
        brelse(bp);
        return addr;
    }
    
    panic("bmap: out of range");
    return 0;
}

static uint32_t bmap(struct inode *ip, uint32_t bn) {
    return bmap_internal(ip, bn, 0);  // Read mode (no CoW)
}

static uint32_t bmap_write(struct inode *ip, uint32_t bn) {
    return bmap_internal(ip, bn, 1);  // Write mode (with CoW)
}

//
// Read/Write inode data
//

int readi(struct inode *ip, void *dst, uint32_t off, uint32_t n) {
    if (off > ip->size || off + n < off) {
        return 0;
    }
    if (off + n > ip->size) {
        n = ip->size - off;
    }
    
    uint32_t total = 0;
    while (total < n) {
        uint32_t bn = off / BSIZE;
        uint32_t addr = bmap(ip, bn);
        if (addr == 0) break;
        
        struct buf *bp = bread(addr);
        uint32_t boff = off % BSIZE;
        uint32_t m = BSIZE - boff;
        if (m > n - total) m = n - total;
        
        memmove(dst, bp->data + boff, m);
        brelse(bp);
        
        total += m;
        off += m;
        dst = (char*)dst + m;
    }
    
    return total;
}

int writei(struct inode *ip, void *src, uint32_t off, uint32_t n) {
    if (off > ip->size || off + n < off) {
        return -1;
    }
    if (off + n > MAXFILE * BSIZE) {
        return -1;
    }
    
    uint32_t total = 0;
    while (total < n) {
        uint32_t bn = off / BSIZE;
        uint32_t addr = bmap_write(ip, bn);  // Use CoW-aware bmap
        if (addr == 0) break;
        
        struct buf *bp = bread(addr);
        uint32_t boff = off % BSIZE;
        uint32_t m = BSIZE - boff;
        if (m > n - total) m = n - total;
        
        memmove(bp->data + boff, src, m);
        bwrite(bp);
        brelse(bp);
        
        total += m;
        off += m;
        src = (char*)src + m;
    }
    
    if (off > ip->size) {
        ip->size = off;
    }
    
    iupdate(ip);
    return total;
}

//
// Directory operations
//

// Look for a directory entry in dp.
struct inode* dirlookup(struct inode *dp, char *name, uint32_t *poff) {
    if (dp->type != T_DIR) {
        panic("dirlookup: not a directory");
    }
    
    struct dirent de;
    for (uint32_t off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, &de, off, sizeof(de)) != sizeof(de)) {
            panic("dirlookup: read error");
        }
        
        if (de.inum == 0) continue;
        
        // Compare names (null-terminated)
        int match = 1;
        for (int i = 0; i < DIRENT_NAMELEN; i++) {
            if (name[i] != de.name[i]) {
                match = 0;
                break;
            }
            if (name[i] == 0) break;
        }
        
        if (match) {
            if (poff) *poff = off;
            return iget(de.inum);
        }
    }
    
    return 0;
}

// Write a new directory entry (name, inum) into directory dp.
int dirlink(struct inode *dp, char *name, uint32_t inum) {
    struct inode *ip;
    
    // Check that name doesn't already exist
    if ((ip = dirlookup(dp, name, 0)) != 0) {
        iput(ip);
        return -1;
    }
    
    // Find an empty slot
    struct dirent de;
    uint32_t off;
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, &de, off, sizeof(de)) != sizeof(de)) {
            panic("dirlink: read error");
        }
        if (de.inum == 0) break;
    }
    
    // Write new entry
    de.inum = inum;
    memzero(de.name, DIRENT_NAMELEN);
    for (int i = 0; i < DIRENT_NAMELEN - 1 && name[i]; i++) {
        de.name[i] = name[i];
    }
    
    if (writei(dp, &de, off, sizeof(de)) != sizeof(de)) {
        return -1;
    }
    
    return 0;
}

//
// Path resolution
//

// Skip leading slashes and get next path element.
// Returns pointer to element, sets *name to element start.
static char* skipelem(char *path, char *name) {
    // Skip leading slashes
    while (*path == '/') path++;
    
    if (*path == 0) return 0;
    
    char *s = path;
    while (*path != '/' && *path != 0) path++;
    
    int len = path - s;
    if (len >= DIRENT_NAMELEN) {
        len = DIRENT_NAMELEN - 1;
    }
    memmove(name, s, len);
    name[len] = 0;
    
    // Skip trailing slashes
    while (*path == '/') path++;
    
    return path;
}

// Look up and return the inode for a path name.
// If nameiparent != 0, return the inode for the parent and copy the final
// path element into name.
static struct inode* namex(char *path, int nameiparent, char *name) {
    struct inode *ip, *next;
    
    if (*path == '/') {
        ip = iget(ROOTINO);
    } else {
        // For now, always start from root (no current working directory)
        ip = iget(ROOTINO);
    }
    
    while ((path = skipelem(path, name)) != 0) {
        ilock(ip);
        
        if (ip->type != T_DIR) {
            iunlock(ip);
            iput(ip);
            return 0;
        }
        
        if (nameiparent && *path == 0) {
            // Stop one level early
            iunlock(ip);
            return ip;
        }
        
        next = dirlookup(ip, name, 0);
        if (next == 0) {
            iunlock(ip);
            iput(ip);
            return 0;
        }
        
        iunlock(ip);
        iput(ip);
        ip = next;
    }
    
    if (nameiparent) {
        iput(ip);
        return 0;
    }
    
    return ip;
}

struct inode* namei(char *path) {
    char name[DIRENT_NAMELEN];
    return namex(path, 0, name);
}

struct inode* nameiparent(char *path, char *name) {
    return namex(path, 1, name);
}

//
// High-level file operations
//

// Create a new file or directory
struct inode* create(char *path, uint16_t type) {
    char name[DIRENT_NAMELEN];
    struct inode *dp, *ip;
    
    // Get parent directory
    if ((dp = nameiparent(path, name)) == 0) {
        return 0;
    }
    
    ilock(dp);
    
    // Check if already exists
    if ((ip = dirlookup(dp, name, 0)) != 0) {
        iunlock(dp);
        iput(dp);
        ilock(ip);
        if (type == T_FILE && ip->type == T_FILE) {
            return ip;  // Return existing file
        }
        iunlock(ip);
        iput(ip);
        return 0;
    }
    
    // Allocate new inode
    if ((ip = ialloc(type)) == 0) {
        iunlock(dp);
        iput(dp);
        return 0;
    }
    
    ilock(ip);
    ip->nlink = 1;
    iupdate(ip);
    
    // If directory, create . and .. entries
    if (type == T_DIR) {
        // . points to self
        if (dirlink(ip, ".", ip->inum) < 0 ||
            dirlink(ip, "..", dp->inum) < 0) {
            // TODO: cleanup on failure
            panic("create: dirlink failed");
        }
    }
    
    // Link into parent
    if (dirlink(dp, name, ip->inum) < 0) {
        // TODO: cleanup on failure
        panic("create: parent dirlink failed");
    }
    
    iunlock(dp);
    iput(dp);
    
    return ip;
}

//
// CoW Clone (Reflink)
//

// Clone an inode - creates a new inode that shares all data blocks
// This is the core of CoW - instant copy by sharing blocks
struct inode* iclone(struct inode *src) {
    if (src->type != T_FILE) {
        kprintf("iclone: can only clone files\n");
        return 0;
    }
    
    // Allocate new inode
    struct inode *dst = ialloc(T_FILE);
    if (dst == 0) {
        return 0;
    }
    
    ilock(dst);
    
    // Copy metadata
    dst->size = src->size;
    dst->nlink = 1;
    
    // Share all data blocks - just copy pointers and increment refcounts
    for (int i = 0; i < NDIRECT; i++) {
        if (src->addrs[i]) {
            dst->addrs[i] = src->addrs[i];
            brefcnt_inc(src->addrs[i]);
        }
    }
    
    // Share indirect block too
    if (src->addrs[NDIRECT]) {
        dst->addrs[NDIRECT] = src->addrs[NDIRECT];
        brefcnt_inc(src->addrs[NDIRECT]);
        
        // Also increment refcounts for blocks in indirect block
        struct buf *bp = bread(src->addrs[NDIRECT]);
        uint32_t *a = (uint32_t *)bp->data;
        for (uint32_t i = 0; i < NINDIRECT; i++) {
            if (a[i]) {
                brefcnt_inc(a[i]);
            }
        }
        brelse(bp);
    }
    
    iupdate(dst);
    
    kprintf("cow: cloned inode %d -> %d (sharing %d bytes)\n", 
            src->inum, dst->inum, src->size);
    
    return dst;
}

