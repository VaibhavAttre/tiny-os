#include <drivers/virtio.h>
#include <drivers/uart.h>
#include "kernel/printf.h"
#include "kernel/kalloc.h"
#include "kernel/string.h"
#include "kernel/panic.h"
#include "mmu.h"

static struct virtio_blk disk;

static inline uint32_t virtio_read(int offset) {
    return disk.regs[offset / 4];
}

static inline void virtio_write(int offset, uint32_t value) {
    disk.regs[offset / 4] = value;
}

static int alloc_desc(void) {
    for (int i = 0; i < VIRTIO_RING_SIZE; i++) {
        if (disk.free[i]) {
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

static void free_desc(int i) {
    if (i < 0 || i >= VIRTIO_RING_SIZE)
        panic("free_desc: bad index");
    if (disk.free[i])
        panic("free_desc: already free");
    disk.free[i] = 1;
}

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

static int alloc3_desc(int *idx) {
    for (int i = 0; i < 3; i++) {
        idx[i] = alloc_desc();
        if (idx[i] < 0) {
            for (int j = 0; j < i; j++)
                free_desc(idx[j]);
            return -1;
        }
    }
    return 0;
}

void virtio_blk_init(void) {
    uint32_t status = 0;

    uint64_t found_addr = 0;
    for (uint64_t addr = VIRTIO0; addr < VIRTIO0 + 8 * VIRTIO_MMIO_SIZE; addr += VIRTIO_MMIO_SIZE) {
        volatile uint32_t *regs = (volatile uint32_t *)addr;

        if (regs[VIRTIO_MMIO_MAGIC_VALUE / 4] != 0x74726976) {
            continue;
        }

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

    uint32_t version = virtio_read(VIRTIO_MMIO_VERSION);
    if (version != 1 && version != 2) {
        kprintf("virtio: unsupported version %d\n", version);
        return;
    }
    kprintf("virtio: MMIO version %d\n", version);

    kprintf("virtio: found block device\n");

    virtio_write(VIRTIO_MMIO_STATUS, 0);

    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_write(VIRTIO_MMIO_STATUS, status);

    status |= VIRTIO_STATUS_DRIVER;
    virtio_write(VIRTIO_MMIO_STATUS, status);

    uint32_t features = virtio_read(VIRTIO_MMIO_DEVICE_FEATURES);
    kprintf("virtio: device features: %p\n", (void*)(uint64_t)features);

    virtio_write(VIRTIO_MMIO_DRIVER_FEATURES, 0);

    if (version == 2) {
        status |= VIRTIO_STATUS_FEATURES_OK;
        virtio_write(VIRTIO_MMIO_STATUS, status);

        if (!(virtio_read(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
            kprintf("virtio: device rejected features\n");
            virtio_write(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
            return;
        }
    }

    if (version == 1) {
        virtio_write(VIRTIO_MMIO_GUEST_PAGE_SIZE, PGSIZE);
    }

    virtio_write(VIRTIO_MMIO_QUEUE_SEL, 0);

    if (version == 2 && virtio_read(VIRTIO_MMIO_QUEUE_READY)) {
        kprintf("virtio: queue already in use\n");
        return;
    }

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

    virtio_write(VIRTIO_MMIO_QUEUE_NUM, VIRTIO_RING_SIZE);

    void *page1 = 0;
    void *page2 = 0;
    for (int tries = 0; tries < 128; tries++) {
        page1 = kalloc();
        page2 = kalloc();
        if (!page1 || !page2) {
            kprintf("virtio: no memory for queue\n");
            return;
        }
        if ((uint64_t)page2 + PGSIZE == (uint64_t)page1) {
            void *tmp = page1;
            page1 = page2;
            page2 = tmp;
        }
        if ((uint64_t)page2 == (uint64_t)page1 + PGSIZE) {
            break;
        }
        kfree(page1);
        kfree(page2);
        page1 = 0;
        page2 = 0;
    }
    if (!page1 || !page2 || (uint64_t)page2 != (uint64_t)page1 + PGSIZE) {
        panic("virtio: cannot allocate contiguous queue pages");
    }
    memzero(page1, PGSIZE);
    memzero(page2, PGSIZE);

    disk.desc = (struct virtq_desc *)page1;
    disk.avail = (struct virtq_avail *)((char*)page1 + VIRTIO_RING_SIZE * 16);
    disk.used = (struct virtq_used *)page2;

    if (version == 1) {
        virtio_write(VIRTIO_MMIO_QUEUE_ALIGN, PGSIZE);
        uint64_t pfn = (uint64_t)page1 / PGSIZE;
        virtio_write(VIRTIO_MMIO_QUEUE_PFN, (uint32_t)pfn);
    } else {
        uint64_t desc_addr = (uint64_t)disk.desc;
        uint64_t avail_addr = (uint64_t)disk.avail;
        uint64_t used_addr = (uint64_t)disk.used;

        virtio_write(VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_addr);
        virtio_write(VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));
        virtio_write(VIRTIO_MMIO_QUEUE_DRIVER_LOW, (uint32_t)avail_addr);
        virtio_write(VIRTIO_MMIO_QUEUE_DRIVER_HIGH, (uint32_t)(avail_addr >> 32));
        virtio_write(VIRTIO_MMIO_QUEUE_DEVICE_LOW, (uint32_t)used_addr);
        virtio_write(VIRTIO_MMIO_QUEUE_DEVICE_HIGH, (uint32_t)(used_addr >> 32));

        virtio_write(VIRTIO_MMIO_QUEUE_READY, 1);
    }

    for (int i = 0; i < VIRTIO_RING_SIZE; i++) {
        disk.free[i] = 1;
    }

    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_write(VIRTIO_MMIO_STATUS, status);

    kprintf("virtio: block device initialized\n");
}

static int disk_rw(uint64_t sector, void *buf, int write) {
    int idx[3];

    if (alloc3_desc(idx) < 0) {
        kprintf("disk_rw: no descriptors\n");
        return -1;
    }

    struct virtio_blk_req *req = &disk.inflight[idx[0]].req;
    req->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector = sector;

    disk.desc[idx[0]].addr = (uint64_t)req;
    disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
    disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
    disk.desc[idx[0]].next = idx[1];

    disk.desc[idx[1]].addr = (uint64_t)buf;
    disk.desc[idx[1]].len = SECTOR_SIZE;
    disk.desc[idx[1]].flags = VRING_DESC_F_NEXT;
    if (!write) {
        disk.desc[idx[1]].flags |= VRING_DESC_F_WRITE; // Device writes to buf
    }
    disk.desc[idx[1]].next = idx[2];

    disk.inflight[idx[0]].status = 0xff;
    disk.inflight[idx[0]].done = 0;
    disk.desc[idx[2]].addr = (uint64_t)&disk.inflight[idx[0]].status;
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
    disk.desc[idx[2]].next = 0;

    int avail_idx = disk.avail->idx % VIRTIO_RING_SIZE;
    disk.avail->ring[avail_idx] = idx[0];

    __sync_synchronize();

    disk.avail->idx++;

    __sync_synchronize();

    virtio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    while (1) {
        __sync_synchronize();

        if (disk.used->idx != disk.used_idx) {
            while (disk.used_idx != disk.used->idx) {
                int used_slot = disk.used_idx % VIRTIO_RING_SIZE;
                int desc_idx = disk.used->ring[used_slot].id;

                disk.inflight[desc_idx].done = 1;

                disk.used_idx++;
            }
        }

        if (disk.inflight[idx[0]].done) {
            break;
        }
    }

    int status = disk.inflight[idx[0]].status;

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
