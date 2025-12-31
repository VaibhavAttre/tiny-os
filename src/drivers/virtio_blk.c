//
// VirtIO Block Device Driver for QEMU virt machine
//

#include <drivers/virtio.h>
#include <drivers/uart.h>
#include "kernel/printf.h"
#include "kernel/kalloc.h"
#include "kernel/string.h"
#include "kernel/panic.h"
#include "mmu.h"

static struct virtio_blk disk;

// Read a 32-bit MMIO register
static inline uint32_t virtio_read(int offset) {
    return disk.regs[offset / 4];
}

// Write a 32-bit MMIO register
static inline void virtio_write(int offset, uint32_t value) {
    disk.regs[offset / 4] = value;
}

// Allocate a free descriptor
static int alloc_desc(void) {
    for (int i = 0; i < VIRTIO_RING_SIZE; i++) {
        if (disk.free[i]) {
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

// Free a descriptor
static void free_desc(int i) {
    if (i < 0 || i >= VIRTIO_RING_SIZE)
        panic("free_desc: bad index");
    if (disk.free[i])
        panic("free_desc: already free");
    disk.free[i] = 1;
}

// Free a chain of descriptors
static void free_chain(int i) {
    while (1) {
        int next = disk.desc[i].next;
        int flags = disk.desc[i].flags;
        free_desc(i);
        if (!(flags & VRING_DESC_F_NEXT))
            break;
        i = next;
    }
}

// Allocate 3 descriptors for a block request
static int alloc3_desc(int *idx) {
    for (int i = 0; i < 3; i++) {
        idx[i] = alloc_desc();
        if (idx[i] < 0) {
            // Free any we already allocated
            for (int j = 0; j < i; j++)
                free_desc(idx[j]);
            return -1;
        }
    }
    return 0;
}

void virtio_blk_init(void) {
    uint32_t status = 0;
    
    // QEMU virt machine has 8 virtio MMIO slots at 0x10001000 - 0x10008000
    // Probe each one to find the block device
    uint64_t found_addr = 0;
    for (uint64_t addr = VIRTIO0; addr < VIRTIO0 + 8 * VIRTIO_MMIO_SIZE; addr += VIRTIO_MMIO_SIZE) {
        volatile uint32_t *regs = (volatile uint32_t *)addr;
        
        // Check magic
        if (regs[VIRTIO_MMIO_MAGIC_VALUE / 4] != 0x74726976) {
            continue;
        }
        
        // Check if it's a block device
        uint32_t device_id = regs[VIRTIO_MMIO_DEVICE_ID / 4];
        if (device_id == VIRTIO_DEVICE_BLK) {
            found_addr = addr;
            kprintf("virtio: found block device at %p\n", (void*)addr);
            break;
        }
    }
    
    if (found_addr == 0) {
        kprintf("virtio: no block device found\n");
        return;
    }
    
    disk.regs = (volatile uint32_t *)found_addr;
    
    // Check version (1 = legacy, 2 = modern - we support both)
    uint32_t version = virtio_read(VIRTIO_MMIO_VERSION);
    if (version != 1 && version != 2) {
        kprintf("virtio: unsupported version %d\n", version);
        return;
    }
    kprintf("virtio: MMIO version %d\n", version);
    
    kprintf("virtio: found block device\n");
    
    // Reset device
    virtio_write(VIRTIO_MMIO_STATUS, 0);
    
    // Set ACKNOWLEDGE bit
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_write(VIRTIO_MMIO_STATUS, status);
    
    // Set DRIVER bit
    status |= VIRTIO_STATUS_DRIVER;
    virtio_write(VIRTIO_MMIO_STATUS, status);
    
    // Read device features
    uint32_t features = virtio_read(VIRTIO_MMIO_DEVICE_FEATURES);
    kprintf("virtio: device features: %p\n", (void*)(uint64_t)features);
    
    // We only need basic read/write - don't negotiate fancy features
    // For legacy, just accept the default features
    virtio_write(VIRTIO_MMIO_DRIVER_FEATURES, 0);
    
    if (version == 2) {
        // Modern devices need FEATURES_OK
        status |= VIRTIO_STATUS_FEATURES_OK;
        virtio_write(VIRTIO_MMIO_STATUS, status);
        
        if (!(virtio_read(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
            kprintf("virtio: device rejected features\n");
            virtio_write(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
            return;
        }
    }
    
    // For legacy devices, set page size
    if (version == 1) {
        virtio_write(VIRTIO_MMIO_GUEST_PAGE_SIZE, PGSIZE);
    }
    
    // Initialize queue 0
    virtio_write(VIRTIO_MMIO_QUEUE_SEL, 0);
    
    // Check queue not already in use (only for modern)
    if (version == 2 && virtio_read(VIRTIO_MMIO_QUEUE_READY)) {
        kprintf("virtio: queue already in use\n");
        return;
    }
    
    // Check max queue size
    uint32_t max = virtio_read(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0) {
        kprintf("virtio: queue size is 0\n");
        return;
    }
    if (max < VIRTIO_RING_SIZE) {
        kprintf("virtio: queue too small (%d < %d)\n", max, VIRTIO_RING_SIZE);
        return;
    }
    kprintf("virtio: max queue size = %d, using %d\n", max, VIRTIO_RING_SIZE);
    
    // Set queue size
    virtio_write(VIRTIO_MMIO_QUEUE_NUM, VIRTIO_RING_SIZE);
    
    // Allocate queue memory
    // For legacy virtio MMIO, the device calculates ring addresses as:
    //   desc at: pfn * page_size
    //   avail at: desc + 16 * queue_size
    //   used at: align(avail_end, page_size) = next page boundary!
    // So we need 2 pages: page 1 for desc+avail, page 2 for used
    void *page1 = kalloc();
    void *page2 = kalloc();
    if (!page1 || !page2) {
        kprintf("virtio: no memory for queue\n");
        return;
    }
    memzero(page1, PGSIZE);
    memzero(page2, PGSIZE);
    
    // We must give page2 immediately after page1 to the device
    // But kalloc may not give contiguous pages. Check:
    if ((uint64_t)page2 != (uint64_t)page1 + PGSIZE) {
        // Pages not contiguous - swap if page2 < page1, or give up
        if ((uint64_t)page2 < (uint64_t)page1) {
            void *tmp = page1;
            page1 = page2;
            page2 = tmp;
        }
        if ((uint64_t)page2 != (uint64_t)page1 + PGSIZE) {
            kprintf("virtio: pages not contiguous, trying anyway\n");
            // For legacy, we need to register just the first page
            // and hope the device uses our calculated addresses
        }
    }
    
    disk.desc = (struct virtq_desc *)page1;
    disk.avail = (struct virtq_avail *)((char*)page1 + VIRTIO_RING_SIZE * 16);
    disk.used = (struct virtq_used *)page2;
    
    if (version == 1) {
        // Legacy: tell device the page frame number
        uint64_t pfn = (uint64_t)page1 / PGSIZE;
        virtio_write(VIRTIO_MMIO_QUEUE_PFN, (uint32_t)pfn);
    } else {
        // Modern: tell device each address separately
        uint64_t desc_addr = (uint64_t)disk.desc;
        uint64_t avail_addr = (uint64_t)disk.avail;
        uint64_t used_addr = (uint64_t)disk.used;
        
        virtio_write(VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_addr);
        virtio_write(VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));
        virtio_write(VIRTIO_MMIO_QUEUE_DRIVER_LOW, (uint32_t)avail_addr);
        virtio_write(VIRTIO_MMIO_QUEUE_DRIVER_HIGH, (uint32_t)(avail_addr >> 32));
        virtio_write(VIRTIO_MMIO_QUEUE_DEVICE_LOW, (uint32_t)used_addr);
        virtio_write(VIRTIO_MMIO_QUEUE_DEVICE_HIGH, (uint32_t)(used_addr >> 32));
        
        // Queue is ready (modern only)
        virtio_write(VIRTIO_MMIO_QUEUE_READY, 1);
    }
    
    // Mark all descriptors as free
    for (int i = 0; i < VIRTIO_RING_SIZE; i++) {
        disk.free[i] = 1;
    }
    
    // Set DRIVER_OK - device is live
    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_write(VIRTIO_MMIO_STATUS, status);
    
    kprintf("virtio: block device initialized\n");
}

// Perform a disk read or write
static int disk_rw(uint64_t sector, void *buf, int write) {
    int idx[3];
    
    // Allocate 3 descriptors: header, data, status
    if (alloc3_desc(idx) < 0) {
        kprintf("disk_rw: no descriptors\n");
        return -1;
    }
    
    // Set up request header
    struct virtio_blk_req *req = &disk.inflight[idx[0]].req;
    req->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector = sector;
    
    // Descriptor 0: request header (device reads)
    disk.desc[idx[0]].addr = (uint64_t)req;
    disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
    disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
    disk.desc[idx[0]].next = idx[1];
    
    // Descriptor 1: data buffer
    disk.desc[idx[1]].addr = (uint64_t)buf;
    disk.desc[idx[1]].len = SECTOR_SIZE;
    disk.desc[idx[1]].flags = VRING_DESC_F_NEXT;
    if (!write) {
        disk.desc[idx[1]].flags |= VRING_DESC_F_WRITE;  // Device writes to buf
    }
    disk.desc[idx[1]].next = idx[2];
    
    // Descriptor 2: status byte (device writes)
    disk.inflight[idx[0]].status = 0xff;
    disk.inflight[idx[0]].done = 0;
    disk.desc[idx[2]].addr = (uint64_t)&disk.inflight[idx[0]].status;
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
    disk.desc[idx[2]].next = 0;
    
    // Add to available ring
    int avail_idx = disk.avail->idx % VIRTIO_RING_SIZE;
    disk.avail->ring[avail_idx] = idx[0];
    
    // Memory barrier before updating avail->idx
    __sync_synchronize();
    
    disk.avail->idx++;
    
    // Memory barrier before notifying device
    __sync_synchronize();
    
    // Notify device (queue 0)
    virtio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    
    // Poll for completion
    while (1) {
        // Memory barrier
        __sync_synchronize();
        
        // Check if device has processed our request
        if (disk.used->idx != disk.used_idx) {
            // Find our completed request
            while (disk.used_idx != disk.used->idx) {
                int used_slot = disk.used_idx % VIRTIO_RING_SIZE;
                int desc_idx = disk.used->ring[used_slot].id;
                
                // Mark as done
                disk.inflight[desc_idx].done = 1;
                
                disk.used_idx++;
            }
        }
        
        // Check if our request is done
        if (disk.inflight[idx[0]].done) {
            break;
        }
    }
    
    // Check status
    int status = disk.inflight[idx[0]].status;
    
    // Free descriptors
    free_chain(idx[0]);
    
    if (status != VIRTIO_BLK_S_OK) {
        kprintf("disk_rw: error status %d\n", status);
        return -1;
    }
    
    return 0;
}

int disk_read(uint64_t sector, void *buf) {
    return disk_rw(sector, buf, 0);
}

int disk_write(uint64_t sector, void *buf) {
    return disk_rw(sector, buf, 1);
}

