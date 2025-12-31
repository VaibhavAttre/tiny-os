//
// Buffer Cache Implementation
//
// LRU cache of disk blocks. When a block is needed:
// 1. Check if already in cache (return cached copy)
// 2. If not, evict LRU unused buffer and read from disk
//
// Buffers are reference-counted. A buffer with refcnt > 0
// cannot be evicted.
//

#include <kernel/buf.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <drivers/virtio.h>

// The buffer cache
static struct {
    struct buf buf[NBUF];
    
    // Linked list of all buffers, through prev/next.
    // head.next is most recently used.
    // head.prev is least recently used.
    struct buf head;
} bcache;

void binit(void) {
    struct buf *b;
    
    // Create linked list of buffers
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    
    for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->refcnt = 0;
        b->flags = 0;
        b->blockno = 0;
        
        // Insert at head of list (all start as "recently used")
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
    
    kprintf("buf: cache initialized with %d buffers\n", NBUF);
}

// Look for a buffer in the cache.
// If found, increase refcnt and return it.
// If not found, recycle an unused buffer.
static struct buf* bget(uint32_t blockno) {
    struct buf *b;
    
    // Is the block already cached?
    for (b = bcache.head.next; b != &bcache.head; b = b->next) {
        if (b->blockno == blockno && (b->flags & B_VALID)) {
            b->refcnt++;
            return b;
        }
    }
    
    // Not cached. Find an unused buffer to recycle.
    // Start from LRU end (head.prev)
    for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if (b->refcnt == 0) {
            // Found one. If dirty, write it back first.
            if (b->flags & B_DIRTY) {
                // Write back to disk (2 sectors per block)
                uint32_t sector = blockno * (BSIZE / SECTOR_SIZE);
                for (int i = 0; i < BSIZE / SECTOR_SIZE; i++) {
                    disk_write(sector + i, b->data + i * SECTOR_SIZE);
                }
            }
            
            // Repurpose this buffer
            b->blockno = blockno;
            b->flags = 0;  // Not valid yet, will be read
            b->refcnt = 1;
            return b;
        }
    }
    
    panic("bget: no buffers available");
    return 0;
}

// Read a block from disk into cache.
// Returns a locked buffer with valid data.
struct buf* bread(uint32_t blockno) {
    struct buf *b;
    
    b = bget(blockno);
    
    if (!(b->flags & B_VALID)) {
        // Read from disk (2 sectors per block)
        uint32_t sector = blockno * (BSIZE / SECTOR_SIZE);
        for (int i = 0; i < BSIZE / SECTOR_SIZE; i++) {
            disk_read(sector + i, b->data + i * SECTOR_SIZE);
        }
        b->flags |= B_VALID;
    }
    
    // Move to front of LRU list (most recently used)
    b->prev->next = b->next;
    b->next->prev = b->prev;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
    
    return b;
}

// Write buffer's data to disk.
// Caller must hold reference to buffer.
void bwrite(struct buf *b) {
    if (b->refcnt < 1) {
        panic("bwrite: buffer not held");
    }
    
    // Write to disk (2 sectors per block)
    uint32_t sector = b->blockno * (BSIZE / SECTOR_SIZE);
    for (int i = 0; i < BSIZE / SECTOR_SIZE; i++) {
        disk_write(sector + i, b->data + i * SECTOR_SIZE);
    }
    
    b->flags &= ~B_DIRTY;  // No longer dirty
}

// Release a buffer.
// Decrements refcnt. Buffer can be reused when refcnt reaches 0.
void brelse(struct buf *b) {
    if (b->refcnt < 1) {
        panic("brelse: buffer not held");
    }
    
    b->refcnt--;
}

// Mark buffer as dirty (needs write-back).
void bmark_dirty(struct buf *b) {
    b->flags |= B_DIRTY;
}

