#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#define BSIZE       1024
#define FS_MAGIC    0x434F5746  // "COWF"

#define T_DIR       1
#define T_FILE      2

#define NDIRECT     12
#define NINDIRECT   (BSIZE / sizeof(uint32_t))

#define DIRENT_NAMELEN 28

struct superblock {
    uint32_t magic;
    uint32_t version;
    uint32_t nblocks;
    uint32_t ninodes;
    uint32_t nbitmap;
    uint32_t nrefcnt;      // Number of refcount blocks (for CoW)
    uint32_t inode_start;
    uint32_t data_start;
    uint32_t root_ino;
};

struct dinode {
    uint16_t type;
    uint16_t nlink;
    uint32_t size;
    uint32_t refcnt;
    uint32_t addrs[NDIRECT + 1];
};

struct dirent {
    uint32_t inum;
    char name[DIRENT_NAMELEN];
};

#define INODES_PER_BLOCK (BSIZE / sizeof(struct dinode))
#define DIRENTS_PER_BLOCK (BSIZE / sizeof(struct dirent))

int fd;
struct superblock sb;
char block[BSIZE];
uint32_t freeblock;

void wsect(uint32_t sec, void *buf) {
    if (lseek(fd, sec * BSIZE, SEEK_SET) != sec * BSIZE) {
        perror("lseek");
        exit(1);
    }
    if (write(fd, buf, BSIZE) != BSIZE) {
        perror("write");
        exit(1);
    }
}

void rsect(uint32_t sec, void *buf) {
    if (lseek(fd, sec * BSIZE, SEEK_SET) != sec * BSIZE) {
        perror("lseek");
        exit(1);
    }
    if (read(fd, buf, BSIZE) != BSIZE) {
        perror("read");
        exit(1);
    }
}

void winode(uint32_t inum, struct dinode *ip) {
    uint32_t bn = sb.inode_start + inum / INODES_PER_BLOCK;
    rsect(bn, block);
    struct dinode *dip = ((struct dinode *)block) + (inum % INODES_PER_BLOCK);
    *dip = *ip;
    wsect(bn, block);
}

void rinode(uint32_t inum, struct dinode *ip) {
    uint32_t bn = sb.inode_start + inum / INODES_PER_BLOCK;
    rsect(bn, block);
    struct dinode *dip = ((struct dinode *)block) + (inum % INODES_PER_BLOCK);
    *ip = *dip;
}

uint32_t balloc(void) {
    uint32_t b = freeblock++;
    
    // Mark in bitmap
    uint32_t bmap_block = 2 + b / (BSIZE * 8);
    rsect(bmap_block, block);
    uint32_t bi = b % (BSIZE * 8);
    block[bi / 8] |= (1 << (bi % 8));
    wsect(bmap_block, block);
    
    return b;
}

void iappend(uint32_t inum, void *data, int n) {
    struct dinode din;
    rinode(inum, &din);
    
    uint32_t off = din.size;
    char *p = data;
    
    while (n > 0) {
        uint32_t bn = off / BSIZE;
        assert(bn < NDIRECT);  // No indirect blocks for now
        
        if (din.addrs[bn] == 0) {
            din.addrs[bn] = balloc();
        }
        
        uint32_t fbn = din.addrs[bn];
        uint32_t boff = off % BSIZE;
        uint32_t m = BSIZE - boff;
        if (m > (uint32_t)n) m = n;
        
        rsect(fbn, block);
        memcpy(block + boff, p, m);
        wsect(fbn, block);
        
        n -= m;
        off += m;
        p += m;
    }
    
    din.size = off;
    winode(inum, &din);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: mkfs <disk.img> <nblocks>\n");
        exit(1);
    }
    
    uint32_t nblocks = atoi(argv[2]);
    if (nblocks < 100) {
        fprintf(stderr, "Disk too small (min 100 blocks)\n");
        exit(1);
    }
    
    printf("mkfs: creating filesystem with %d blocks\n", nblocks);
    
    // Open/create disk image
    fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        perror("open");
        exit(1);
    }
    
    // Calculate layout
    // Refcount blocks: 1 byte per block, 1024 per block
    uint32_t nbitmap = (nblocks + BSIZE * 8 - 1) / (BSIZE * 8);
    uint32_t nrefcnt = (nblocks + BSIZE - 1) / BSIZE;
    uint32_t ninodes = nblocks / 10;  // ~10% of blocks for inodes
    uint32_t ninode_blocks = (ninodes * sizeof(struct dinode) + BSIZE - 1) / BSIZE;
    
    sb.magic = FS_MAGIC;
    sb.version = 1;
    sb.nblocks = nblocks;
    sb.ninodes = ninodes;
    sb.nbitmap = nbitmap;
    sb.nrefcnt = nrefcnt;
    sb.inode_start = 2 + nbitmap + nrefcnt;
    sb.data_start = sb.inode_start + ninode_blocks;
    sb.root_ino = 1;
    
    freeblock = sb.data_start;
    
    printf("  nbitmap=%d, nrefcnt=%d, ninodes=%d, inode_start=%d, data_start=%d\n",
           nbitmap, nrefcnt, ninodes, sb.inode_start, sb.data_start);
    
    // Zero the entire disk
    memset(block, 0, BSIZE);
    for (uint32_t i = 0; i < nblocks; i++) {
        wsect(i, block);
    }
    
    // Write superblock
    memset(block, 0, BSIZE);
    memcpy(block, &sb, sizeof(sb));
    wsect(1, block);
    
    // Mark reserved blocks as used in bitmap
    for (uint32_t b = 0; b < sb.data_start; b++) {
        uint32_t bmap_block = 2 + b / (BSIZE * 8);
        rsect(bmap_block, block);
        uint32_t bi = b % (BSIZE * 8);
        block[bi / 8] |= (1 << (bi % 8));
        wsect(bmap_block, block);
    }
    freeblock = sb.data_start;
    
    // Create root directory (inode 1)
    struct dinode root;
    memset(&root, 0, sizeof(root));
    root.type = T_DIR;
    root.nlink = 1;
    root.refcnt = 1;
    root.size = 0;
    winode(1, &root);
    
    // Add "." and ".." entries to root
    struct dirent de;
    
    memset(&de, 0, sizeof(de));
    de.inum = 1;
    strcpy(de.name, ".");
    iappend(1, &de, sizeof(de));
    
    memset(&de, 0, sizeof(de));
    de.inum = 1;
    strcpy(de.name, "..");
    iappend(1, &de, sizeof(de));
    
    // Update root inode nlink
    rinode(1, &root);
    root.nlink = 2;  // . and ..
    winode(1, &root);
    
    printf("mkfs: done, root directory at inode %d\n", sb.root_ino);
    
    close(fd);
    return 0;
}

