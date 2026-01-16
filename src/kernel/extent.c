#include <kernel/extent.h>
#include <stdint.h>
#include <kernel/btree.h>
#include <kernel/fs.h>
#include <kernel/printf.h>
#include <kernel/buf.h>
#include <kernel/string.h>

#define MAX_DEFERRED 64

static struct extent deferred[MAX_DEFERRED];
static int deferred_n = 0;

static int block_is_free(uint32_t blockno) {
    uint32_t bmap_block = 1 + NSUPER + blockno / (BSIZE * 8);
    struct buf *bp = bread(bmap_block);
    uint32_t bi = blockno % (BSIZE * 8);
    uint32_t m = 1u << (bi % 8);
    int free = (bp->data[bi / 8] & m) == 0;
    brelse(bp);
    return free;
}

static int block_mark_alloc(uint32_t blockno) {
    uint32_t bmap_block = 1 + NSUPER + blockno / (BSIZE * 8);
    struct buf *bp = bread(bmap_block);
    uint32_t bi = blockno % (BSIZE * 8);
    uint32_t m = 1u << (bi % 8);
    if (bp->data[bi / 8] & m) {
        brelse(bp);
        return -1;
    }
    bp->data[bi / 8] |= m;
    bwrite(bp);
    brelse(bp);

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
    return 0;
}

void extent_init(void) {
    if (sb.extent_root != 0) {
        return;
    }

    uint32_t root = 0;
    if (btree_create_empty(0, &root) < 0) {
        kprintf("extent: init failed\n");
        return;
    }
    sb.extent_root = root;
    writesb();
}

int extent_alloc(uint32_t len, struct extent *out) {
    if (sb.extent_root == 0) {
        extent_init();
    }
    if (len == 0) return -1;

    uint64_t k = 0;
    int found = 0;
    for (uint64_t b = sb.data_start; b + len <= sb.nblocks; b++) {
        int ok = 1;
        for (uint32_t i = 0; i < len; i++) {
            if (!block_is_free((uint32_t)b + i)) {
                ok = 0;
                b += i;
                break;
            }
        }
        if (ok) {
            k = b;
            found = 1;
            break;
        }
    }
    if (!found) {
        return -1;
    }

    for (uint32_t i = 0; i < len; i++) {
        if (block_mark_alloc((uint32_t)k + i) < 0) {
            return -1;
        }
    }

    writesb();

    if (out) {
        out->start = (uint32_t)k;
        out->len = len;
    }
    return 0;
}

void extent_free(uint32_t start, uint32_t len) {
    if (len == 0) return;
    if (deferred_n >= MAX_DEFERRED) {
        kprintf("extent: deferred list full\n");
        return;
    }
    deferred[deferred_n].start = start;
    deferred[deferred_n].len = len;
    deferred_n++;
}

int extent_commit(void) {
    if (sb.extent_root == 0) {
        extent_init();
    }
    if (sb.extent_root == 0) {
        return -1;
    }

    for (int i = 0; i < deferred_n; i++) {
        uint64_t start = deferred[i].start;
        uint64_t len = deferred[i].len;
        for (uint32_t b = 0; b < len; b++) {
            bfree((uint32_t)start + b);
        }
    }

    deferred_n = 0;
    writesb();
    return 0;
}
