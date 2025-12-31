#pragma once
#include <stdint.h>

//
// Buffer Cache Layer
// Sits between filesystem and disk driver, providing:
// - Block-level abstraction (BSIZE bytes per block)
// - Caching to reduce disk I/O
// - Dirty tracking for write-back
//

#define BSIZE 1024          // Block size (2 sectors)
#define NBUF  30            // Number of buffers in cache

// Buffer flags
#define B_VALID  0x1        // Buffer contains valid data
#define B_DIRTY  0x2        // Buffer has been modified

struct buf {
    int flags;              // B_VALID, B_DIRTY
    uint32_t blockno;       // Block number on disk
    int refcnt;             // Reference count
    
    struct buf *prev;       // LRU cache list
    struct buf *next;
    
    uint8_t data[BSIZE];    // Block data
};

// Buffer cache API
void            binit(void);
struct buf*     bread(uint32_t blockno);
void            bwrite(struct buf *b);
void            brelse(struct buf *b);
void            bmark_dirty(struct buf *b);

