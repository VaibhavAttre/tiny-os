#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define BSIZE 1024
#define FS_MAGIC    0x434F5746  // "COWF"
#define NSUPER 2

#define T_UNUSED 0
#define T_DIR 1
#define T_FILE 2

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint32_t))

#define DIRENT_NAMELEN 28

struct superblock {
    uint32_t magic;
    uint32_t version;
    uint32_t nblocks;
    uint32_t ninodes;
    uint32_t nbitmap;
    uint32_t nrefcnt;
    uint32_t inode_start;
    uint32_t data_start;
    uint32_t root_ino;
    uint32_t btree_root;
    uint32_t extent_root;
    uint32_t root_tree;
    uint32_t fs_next_ino;
    uint64_t generation;
    uint32_t checksum;
    uint32_t reserved;
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
#define REFCNTS_PER_BLOCK (BSIZE / sizeof(uint8_t))

static int fd;
static char block[BSIZE];

static void rsect(uint32_t sec, void *buf) {
    if (lseek(fd, sec * BSIZE, SEEK_SET) != (off_t)(sec * BSIZE)) {
        perror("lseek");
        exit(1);
    }
    if (read(fd, buf, BSIZE) != BSIZE) {
        perror("read");
        exit(1);
    }
}

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

static int read_superblock(struct superblock *out) {
    struct superblock best;
    memset(&best, 0, sizeof(best));
    uint64_t best_gen = 0;

    for (uint32_t i = 0; i < NSUPER; i++) {
        rsect(1 + i, block);
        struct superblock cand;
        memcpy(&cand, block, sizeof(cand));
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

    if (best.magic != FS_MAGIC) {
        return -1;
    }

    *out = best;
    return 0;
}

static void read_inode(const struct superblock *sb, uint32_t inum,
                       struct dinode *out) {
    uint32_t bn = sb->inode_start + inum / INODES_PER_BLOCK;
    uint32_t off = (inum % INODES_PER_BLOCK) * sizeof(struct dinode);
    rsect(bn, block);
    memcpy(out, block + off, sizeof(*out));
}

static int check_root_dir(const struct superblock *sb, const struct dinode *root) {
    if (root->type != T_DIR || root->size < 2 * sizeof(struct dirent)) {
        return -1;
    }
    uint32_t bno = root->addrs[0];
    if (bno == 0 || bno >= sb->nblocks) {
        return -1;
    }
    rsect(bno, block);
    struct dirent *de = (struct dirent *)block;
    if (de[0].inum != sb->root_ino || strcmp(de[0].name, ".") != 0) {
        return -1;
    }
    if (de[1].inum != sb->root_ino || strcmp(de[1].name, "..") != 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: fsck <disk.img>\n");
        return 1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct superblock sb;
    if (read_superblock(&sb) < 0) {
        fprintf(stderr, "fsck: invalid or corrupt superblock\n");
        close(fd);
        return 1;
    }

    uint32_t bitmap_start = 1 + NSUPER;
    uint32_t refcnt_start = bitmap_start + sb.nbitmap;
    uint32_t inode_end = sb.inode_start + (sb.ninodes + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK;

    if (sb.inode_start != refcnt_start + sb.nrefcnt) {
        fprintf(stderr, "fsck: inode_start mismatch (sb=%u, calc=%u)\n",
                sb.inode_start, refcnt_start + sb.nrefcnt);
    }
    if (sb.data_start < inode_end) {
        fprintf(stderr, "fsck: data_start overlaps inode blocks\n");
    }

    uint8_t *bitmap = calloc(sb.nblocks, sizeof(uint8_t));
    uint8_t *refcnt_disk = calloc(sb.nblocks, sizeof(uint8_t));
    uint16_t *refcnt_calc = calloc(sb.nblocks, sizeof(uint16_t));
    if (!bitmap || !refcnt_disk || !refcnt_calc) {
        fprintf(stderr, "fsck: out of memory\n");
        close(fd);
        return 1;
    }

    for (uint32_t b = 0; b < sb.nblocks; b += BSIZE * 8) {
        uint32_t bmap_block = bitmap_start + b / (BSIZE * 8);
        rsect(bmap_block, block);
        for (uint32_t bi = 0; bi < BSIZE * 8 && b + bi < sb.nblocks; bi++) {
            uint32_t m = 1u << (bi % 8);
            if (block[bi / 8] & m) {
                bitmap[b + bi] = 1;
            }
        }
    }

    for (uint32_t b = 0; b < sb.nblocks; b += REFCNTS_PER_BLOCK) {
        uint32_t ref_block = refcnt_start + (b / REFCNTS_PER_BLOCK);
        rsect(ref_block, block);
        for (uint32_t bi = 0; bi < REFCNTS_PER_BLOCK && b + bi < sb.nblocks; bi++) {
            refcnt_disk[b + bi] = (uint8_t)block[bi];
        }
    }

    int errors = 0;

    for (uint32_t b = 0; b < sb.data_start; b++) {
        if (!bitmap[b]) {
            fprintf(stderr, "fsck: metadata block %u not marked allocated\n", b);
            errors++;
        }
    }

    for (uint32_t inum = 1; inum < sb.ninodes; inum++) {
        struct dinode din;
        read_inode(&sb, inum, &din);

        if (din.type == T_UNUSED) {
            continue;
        }
        if (din.type != T_DIR && din.type != T_FILE) {
            fprintf(stderr, "fsck: inode %u has invalid type %u\n", inum, din.type);
            errors++;
            continue;
        }

        uint32_t nblocks = (din.size + BSIZE - 1) / BSIZE;

        for (uint32_t i = 0; i < NDIRECT && i < nblocks; i++) {
            uint32_t bno = din.addrs[i];
            if (bno == 0 || bno < sb.data_start || bno >= sb.nblocks) {
                fprintf(stderr, "fsck: inode %u bad direct block %u\n", inum, bno);
                errors++;
                continue;
            }
            refcnt_calc[bno]++;
            if (!bitmap[bno]) {
                fprintf(stderr, "fsck: inode %u block %u not marked allocated\n", inum, bno);
                errors++;
            }
        }

        if (nblocks > NDIRECT) {
            uint32_t ib = din.addrs[NDIRECT];
            if (ib == 0 || ib < sb.data_start || ib >= sb.nblocks) {
                fprintf(stderr, "fsck: inode %u bad indirect block %u\n", inum, ib);
                errors++;
                continue;
            }
            refcnt_calc[ib]++;
            if (!bitmap[ib]) {
                fprintf(stderr, "fsck: inode %u indirect block %u not allocated\n", inum, ib);
                errors++;
            }

            rsect(ib, block);
            uint32_t *a = (uint32_t *)block;
            uint32_t end = nblocks - NDIRECT;
            if (end > NINDIRECT) {
                end = NINDIRECT;
            }
            for (uint32_t i = 0; i < end; i++) {
                uint32_t bno = a[i];
                if (bno == 0 || bno < sb.data_start || bno >= sb.nblocks) {
                    fprintf(stderr, "fsck: inode %u bad indirect data block %u\n",
                            inum, bno);
                    errors++;
                    continue;
                }
                refcnt_calc[bno]++;
                if (!bitmap[bno]) {
                    fprintf(stderr, "fsck: inode %u block %u not allocated\n", inum, bno);
                    errors++;
                }
            }
        }
    }

    for (uint32_t b = sb.data_start; b < sb.nblocks; b++) {
        if (refcnt_calc[b] > 255) {
            fprintf(stderr, "fsck: block %u refcount overflow (%u)\n",
                    b, refcnt_calc[b]);
            errors++;
        }
        if (refcnt_disk[b] != (uint8_t)refcnt_calc[b]) {
            fprintf(stderr, "fsck: block %u refcount mismatch (disk=%u calc=%u)\n",
                    b, refcnt_disk[b], refcnt_calc[b]);
            errors++;
        }
    }

    struct dinode root;
    read_inode(&sb, sb.root_ino, &root);
    if (check_root_dir(&sb, &root) < 0) {
        fprintf(stderr, "fsck: root directory invalid\n");
        errors++;
    }

    if (errors == 0) {
        printf("fsck: clean\n");
    } else {
        printf("fsck: %d issue(s) found\n", errors);
    }

    free(bitmap);
    free(refcnt_disk);
    free(refcnt_calc);
    close(fd);
    return (errors == 0) ? 0 : 1;
}
