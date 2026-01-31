
#include <kernel/buf.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <drivers/virtio.h>

static struct {
    struct buf buf[NBUF];

    struct buf head;
} bcache;

void binit(void) {
    struct buf *b;

    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;

    for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->refcnt = 0;
        b->flags = 0;
        b->blockno = 0;

        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }

    kprintf("buf: cache initialized with %d buffers\n", NBUF);
}

static struct buf* bget(uint32_t blockno) {
    struct buf *b;

    for (b = bcache.head.next; b != &bcache.head; b = b->next) {
        if (b->blockno == blockno && (b->flags & B_VALID)) {
            b->refcnt++;
            return b;
        }
    }

    for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if (b->refcnt == 0) {
            if (b->flags & B_DIRTY) {
                uint32_t sector = blockno * (BSIZE / SECTOR_SIZE);
                for (int i = 0; i < BSIZE / SECTOR_SIZE; i++) {
                    disk_write(sector + i, b->data + i * SECTOR_SIZE);
                }
            }

            b->blockno = blockno;
            b->flags = 0; // Not valid yet, will be read
            b->refcnt = 1;
            return b;
        }
    }

    panic("bget: no buffers available");
    return 0;
}

struct buf* bread(uint32_t blockno) {
    struct buf *b;

    b = bget(blockno);

    if (!(b->flags & B_VALID)) {
        uint32_t sector = blockno * (BSIZE / SECTOR_SIZE);
        for (int i = 0; i < BSIZE / SECTOR_SIZE; i++) {
            disk_read(sector + i, b->data + i * SECTOR_SIZE);
        }
        b->flags |= B_VALID;
    }

    b->prev->next = b->next;
    b->next->prev = b->prev;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;

    return b;
}

void bwrite(struct buf *b) {
    if (b->refcnt < 1) {
        panic("bwrite: buffer not held");
    }

    uint32_t sector = b->blockno * (BSIZE / SECTOR_SIZE);
    for (int i = 0; i < BSIZE / SECTOR_SIZE; i++) {
        disk_write(sector + i, b->data + i * SECTOR_SIZE);
    }

    b->flags &= ~B_DIRTY; // No longer dirty
}

void brelse(struct buf *b) {
    if (b->refcnt < 1) {
        panic("brelse: buffer not held");
    }

    b->refcnt--;
}

void bmark_dirty(struct buf *b) {
    b->flags |= B_DIRTY;
}

