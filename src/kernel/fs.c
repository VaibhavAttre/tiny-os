
#include <kernel/fs.h>
#include <kernel/buf.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <kernel/extent.h>

struct superblock sb;

static uint32_t sb_checksum(const struct superblock *sbp) {
    struct superblock tmp = *sbp;
    tmp.checksum = 0;
    tmp.reserved = 0;

    const uint8_t *p = (const uint8_t *)&tmp;
    uint32_t hash = 2166136261u; // FNV-1a
    for (uint32_t i = 0; i < sizeof(tmp); i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

void readsb(void) {
    struct superblock best;
    memzero(&best, sizeof(best));
    uint64_t best_gen = 0;

    for (uint32_t i = 0; i < NSUPER; i++) {
        struct buf *bp = bread(1 + i);
        struct superblock cand;
        memmove(&cand, bp->data, sizeof(cand));
        brelse(bp);

        if (cand.magic != FS_MAGIC) {
            continue;
        }
        if (sb_checksum(&cand) != cand.checksum) {
            continue;
        }
        if (cand.generation >= best_gen) {
            best = cand;
            best_gen = cand.generation;
        }
    }

    memmove(&sb, &best, sizeof(sb));
}

void writesb(void) {
    sb.generation++;
    sb.checksum = sb_checksum(&sb);

    for (uint32_t i = 0; i < NSUPER; i++) {
        struct buf *bp = bread(1 + i);
        memzero(bp->data, BSIZE);
        memmove(bp->data, &sb, sizeof(sb));
        bwrite(bp);
        brelse(bp);
    }
}

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

uint8_t brefcnt_get(uint32_t blockno) {
    if (blockno < sb.data_start || blockno >= sb.nblocks) {
        return 0;
    }

    uint32_t refcnt_block = 1 + NSUPER + sb.nbitmap +
                            (blockno / REFCNTS_PER_BLOCK);
    struct buf *bp = bread(refcnt_block);
    uint8_t refcnt = bp->data[blockno % REFCNTS_PER_BLOCK];
    brelse(bp);
    return refcnt;
}

void brefcnt_inc(uint32_t blockno) {
    if (blockno < sb.data_start || blockno >= sb.nblocks) {
        return;
    }

    uint32_t refcnt_block = 1 + NSUPER + sb.nbitmap +
                            (blockno / REFCNTS_PER_BLOCK);
    struct buf *bp = bread(refcnt_block);
    uint32_t idx = blockno % REFCNTS_PER_BLOCK;

    if (bp->data[idx] < 255) {
        bp->data[idx]++;
        bwrite(bp);
    }
    brelse(bp);
}

void brefcnt_dec(uint32_t blockno) {
    if (blockno < sb.data_start || blockno >= sb.nblocks) {
        return;
    }

    uint32_t refcnt_block = 1 + NSUPER + sb.nbitmap +
                            (blockno / REFCNTS_PER_BLOCK);
    struct buf *bp = bread(refcnt_block);
    uint32_t idx = blockno % REFCNTS_PER_BLOCK;

    if (bp->data[idx] > 0) {
        bp->data[idx]--;
        bwrite(bp);

        if (bp->data[idx] == 0) {
            brelse(bp);
            uint32_t bmap_block = 1 + NSUPER + blockno / (BSIZE * 8);
            bp = bread(bmap_block);
            uint32_t bi = blockno % (BSIZE * 8);
            bp->data[bi / 8] &= ~(1 << (bi % 8));
            bwrite(bp);
        }
    }
    brelse(bp);
}

uint32_t balloc(void) {
    struct buf *bp;

    if (sb.extent_root != 0) {
        struct extent ex;
        if (extent_alloc_meta(1, &ex) == 0) {
            return ex.start;
        }
    }

    if (extent_meta_active()) {
        uint32_t blocks_per_map = BSIZE * 8;
        int64_t last_map = (sb.nblocks - 1) / blocks_per_map;
        for (int64_t map = last_map; map >= 0; map--) {
            uint32_t bmap_block = 1 + NSUPER + (uint32_t)map;
            bp = bread(bmap_block);
            int64_t limit = blocks_per_map - 1;
            if (map == last_map) {
                limit = (sb.nblocks - 1) - (map * blocks_per_map);
            }
            for (int64_t bi = limit; bi >= 0; bi--) {
                uint32_t m = 1u << (bi % 8);
                if ((bp->data[bi / 8] & m) == 0) {
                    bp->data[bi / 8] |= m;
                    bwrite(bp);
                    brelse(bp);

                    uint32_t blockno = (uint32_t)(map * blocks_per_map + bi);
                    uint32_t refcnt_block = 1 + NSUPER + sb.nbitmap +
                                            (blockno / REFCNTS_PER_BLOCK);
                    bp = bread(refcnt_block);
                    bp->data[blockno % REFCNTS_PER_BLOCK] = 1;
                    bwrite(bp);
                    brelse(bp);

                    bp = bread(blockno);
                    memzero(bp->data, BSIZE);
                    bwrite(bp);
                    brelse(bp);

                    return blockno;
                }
            }
            brelse(bp);
        }
    }

    for (uint32_t b = 0; b < sb.nblocks; b += BSIZE * 8) {
        uint32_t bmap_block = 1 + NSUPER + b / (BSIZE * 8);
        bp = bread(bmap_block);

        for (uint32_t bi = 0; bi < BSIZE * 8 && b + bi < sb.nblocks; bi++) {
            uint32_t m = 1 << (bi % 8);

            if ((bp->data[bi / 8] & m) == 0) {
                bp->data[bi / 8] |= m;
                bwrite(bp);
                brelse(bp);

                uint32_t blockno = b + bi;

                uint32_t refcnt_block = 1 + NSUPER + sb.nbitmap +
                                        (blockno / REFCNTS_PER_BLOCK);
                bp = bread(refcnt_block);
                bp->data[blockno % REFCNTS_PER_BLOCK] = 1;
                bwrite(bp);
                brelse(bp);

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

void bfree(uint32_t blockno) {
    if (blockno == 0) return;
    if (blockno >= sb.nblocks) {
        panic("bfree: block out of range");
    }

    brefcnt_dec(blockno);
}

#define NINODE 50 // Maximum number of cached inodes

static struct {
    struct inode inode[NINODE];
} icache;

struct inode* iget(uint32_t inum) {
    struct inode *ip, *empty = 0;

    for (ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {
        if (ip->refcnt > 0 && ip->inum == inum) {
            ip->refcnt++;
            return ip;
        }
        if (empty == 0 && ip->refcnt == 0) {
            empty = ip;
        }
    }

    if (empty == 0) {
        panic("iget: no inodes available");
    }

    ip = empty;
    ip->inum = inum;
    ip->refcnt = 1;
    ip->valid = 0;

    return ip;
}

struct inode* idup(struct inode *ip) {
    if (ip) {
        ip->refcnt++;
    }
    return ip;
}

void iput(struct inode *ip) {
    if (!ip) return;
    if (ip->refcnt < 1) {
        panic("iput: refcnt < 1");
    }
    ip->refcnt--;
}

void ilock(struct inode *ip) {
    if (ip == 0 || ip->refcnt < 1) {
        panic("ilock");
    }

    if (ip->valid == 0) {
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

void iunlock(struct inode *ip) {
    if (ip == 0 || ip->refcnt < 1) {
        panic("iunlock");
    }
}

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

void itrunc(struct inode *ip) {
    if (ip == 0 || ip->refcnt < 1) {
        panic("itrunc");
    }

    for (int i = 0; i < NDIRECT; i++) {
        if (ip->addrs[i]) {
            bfree(ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    if (ip->addrs[NDIRECT]) {
        struct buf *bp = bread(ip->addrs[NDIRECT]);
        uint32_t *a = (uint32_t *)bp->data;
        for (int i = 0; i < NINDIRECT; i++) {
            if (a[i]) {
                bfree(a[i]);
                a[i] = 0;
            }
        }
        brelse(bp);
        bfree(ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }

    ip->size = 0;
    iupdate(ip);
}

void itrunc_to(struct inode *ip, uint32_t newsize) {
    if (ip == 0 || ip->refcnt < 1) {
        panic("itrunc_to");
    }
    if (newsize >= ip->size) {
        return;
    }

    uint32_t old_nblocks = (ip->size + BSIZE - 1) / BSIZE;
    uint32_t new_nblocks = (newsize + BSIZE - 1) / BSIZE;

    for (uint32_t i = new_nblocks; i < old_nblocks && i < NDIRECT; i++) {
        if (ip->addrs[i]) {
            bfree(ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    if (old_nblocks > NDIRECT && ip->addrs[NDIRECT]) {
        struct buf *bp = bread(ip->addrs[NDIRECT]);
        uint32_t *a = (uint32_t *)bp->data;

        uint32_t start = 0;
        if (new_nblocks > NDIRECT) {
            start = new_nblocks - NDIRECT;
        }
        uint32_t end = old_nblocks - NDIRECT;
        if (end > NINDIRECT) {
            end = NINDIRECT;
        }

        for (uint32_t i = start; i < end; i++) {
            if (a[i]) {
                bfree(a[i]);
                a[i] = 0;
            }
        }

        int keep = 0;
        for (uint32_t i = 0; i < NINDIRECT; i++) {
            if (a[i]) {
                keep = 1;
                break;
            }
        }

        if (keep) {
            bwrite(bp);
            brelse(bp);
        } else {
            brelse(bp);
            bfree(ip->addrs[NDIRECT]);
            ip->addrs[NDIRECT] = 0;
        }
    }

    ip->size = newsize;
    iupdate(ip);
}

struct inode* ialloc(uint16_t type) {
    for (uint32_t inum = 1; inum < sb.ninodes; inum++) {
        uint32_t block = sb.inode_start + inum / INODES_PER_BLOCK;
        uint32_t offset = (inum % INODES_PER_BLOCK) * sizeof(struct dinode);

        struct buf *bp = bread(block);
        struct dinode *dip = (struct dinode *)(bp->data + offset);

        if (dip->type == T_UNUSED) {
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

static uint32_t bcopy_cow(uint32_t oldblock) {
    uint32_t newblock = balloc();
    if (newblock == 0) return 0;

    struct buf *old_bp = bread(oldblock);
    struct buf *new_bp = bread(newblock);
    memmove(new_bp->data, old_bp->data, BSIZE);
    bwrite(new_bp);
    brelse(new_bp);
    brelse(old_bp);

    brefcnt_dec(oldblock);

    return newblock;
}

static uint32_t bmap_internal(struct inode *ip, uint32_t bn, int forwrite) {
    uint32_t addr;
    struct buf *bp;

    if (bn < NDIRECT) {
        if ((addr = ip->addrs[bn]) == 0) {
            addr = balloc();
            if (addr == 0) return 0;
            ip->addrs[bn] = addr;
        } else if (forwrite && brefcnt_get(addr) > 1) {
            addr = bcopy_cow(addr);
            if (addr == 0) return 0;
            ip->addrs[bn] = addr;
            iupdate(ip);
        }
        return addr;
    }

    bn -= NDIRECT;

    if (bn < NINDIRECT) {
        if ((addr = ip->addrs[NDIRECT]) == 0) {
            addr = balloc();
            if (addr == 0) return 0;
            ip->addrs[NDIRECT] = addr;
        } else if (forwrite && brefcnt_get(addr) > 1) {
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
    return bmap_internal(ip, bn, 0); // Read mode (no CoW)
}

static uint32_t bmap_write(struct inode *ip, uint32_t bn) {
    return bmap_internal(ip, bn, 1); // Write mode (with CoW)
}

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
        uint32_t addr = bmap_write(ip, bn); // Use CoW-aware bmap
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

int dirlink(struct inode *dp, char *name, uint32_t inum) {
    struct inode *ip;

    if ((ip = dirlookup(dp, name, 0)) != 0) {
        iput(ip);
        return -1;
    }

    struct dirent de;
    uint32_t off;
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, &de, off, sizeof(de)) != sizeof(de)) {
            panic("dirlink: read error");
        }
        if (de.inum == 0) break;
    }

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

static char* skipelem(char *path, char *name) {
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

    while (*path == '/') path++;

    return path;
}

static struct inode* namex(char *path, int nameiparent, char *name) {
    struct inode *ip, *next;

    if (*path == '/') {
        ip = iget(ROOTINO);
    } else {
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

struct inode* create(char *path, uint16_t type) {
    char name[DIRENT_NAMELEN];
    struct inode *dp, *ip;

    if ((dp = nameiparent(path, name)) == 0) {
        return 0;
    }

    ilock(dp);

    if ((ip = dirlookup(dp, name, 0)) != 0) {
        iunlock(dp);
        iput(dp);
        ilock(ip);
        if (type == T_FILE && ip->type == T_FILE) {
            return ip; // Return existing file
        }
        iunlock(ip);
        iput(ip);
        return 0;
    }

    if ((ip = ialloc(type)) == 0) {
        iunlock(dp);
        iput(dp);
        return 0;
    }

    ilock(ip);
    ip->nlink = 1;
    iupdate(ip);

    if (type == T_DIR) {
        if (dirlink(ip, ".", ip->inum) < 0 ||
            dirlink(ip, "..", dp->inum) < 0) {
            panic("create: dirlink failed");
        }
    }

    if (dirlink(dp, name, ip->inum) < 0) {
        panic("create: parent dirlink failed");
    }

    iunlock(dp);
    iput(dp);

    return ip;
}

struct inode* iclone(struct inode *src) {
    if (src->type != T_FILE) {
        kprintf("iclone: can only clone files\n");
        return 0;
    }

    struct inode *dst = ialloc(T_FILE);
    if (dst == 0) {
        return 0;
    }

    ilock(dst);

    dst->size = src->size;
    dst->nlink = 1;

    for (int i = 0; i < NDIRECT; i++) {
        if (src->addrs[i]) {
            dst->addrs[i] = src->addrs[i];
            brefcnt_inc(src->addrs[i]);
        }
    }

    if (src->addrs[NDIRECT]) {
        dst->addrs[NDIRECT] = src->addrs[NDIRECT];
        brefcnt_inc(src->addrs[NDIRECT]);

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
