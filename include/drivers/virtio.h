#pragma once
#include <stdint.h>

// VirtIO MMIO interface for QEMU virt machine
// Reference: https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.html

// QEMU virt machine has 8 VirtIO MMIO devices at these addresses
#define VIRTIO0             0x10001000
#define VIRTIO_MMIO_SIZE    0x1000

// VirtIO MMIO register offsets
#define VIRTIO_MMIO_MAGIC_VALUE         0x000  // 0x74726976 ("virt")
#define VIRTIO_MMIO_VERSION             0x004  // Version (2 for modern)
#define VIRTIO_MMIO_DEVICE_ID           0x008  // Device type
#define VIRTIO_MMIO_VENDOR_ID           0x00c  // Vendor ID
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010  // Device features (read)
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014  // Device features selector
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020  // Driver features (write)
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024  // Driver features selector
#define VIRTIO_MMIO_QUEUE_SEL           0x030  // Queue selector
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034  // Max queue size
#define VIRTIO_MMIO_QUEUE_NUM           0x038  // Current queue size
#define VIRTIO_MMIO_QUEUE_ALIGN         0x03c  // Queue alignment (legacy)
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028  // Guest page size (legacy)
#define VIRTIO_MMIO_QUEUE_PFN           0x040  // Queue PFN (legacy)
#define VIRTIO_MMIO_QUEUE_READY         0x044  // Queue ready (modern)
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050  // Queue notification
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060  // Interrupt status
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064  // Interrupt acknowledge
#define VIRTIO_MMIO_STATUS              0x070  // Device status
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080  // Descriptor table addr (low)
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084  // Descriptor table addr (high)
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW    0x090  // Available ring addr (low)
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH   0x094  // Available ring addr (high)
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW    0x0a0  // Used ring addr (low)
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH   0x0a4  // Used ring addr (high)

// Device status bits
#define VIRTIO_STATUS_ACKNOWLEDGE       1
#define VIRTIO_STATUS_DRIVER            2
#define VIRTIO_STATUS_DRIVER_OK         4
#define VIRTIO_STATUS_FEATURES_OK       8
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED            128

// Device types
#define VIRTIO_DEVICE_NET       1
#define VIRTIO_DEVICE_BLK       2
#define VIRTIO_DEVICE_CONSOLE   3
#define VIRTIO_DEVICE_RNG       4

// Feature bits
#define VIRTIO_F_RING_INDIRECT_DESC     28
#define VIRTIO_F_RING_EVENT_IDX         29
#define VIRTIO_F_VERSION_1              32

// VirtIO block device feature bits
#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

// Virtqueue descriptor flags
#define VRING_DESC_F_NEXT       1   // Buffer continues in next descriptor
#define VRING_DESC_F_WRITE      2   // Buffer is write-only (device writes)
#define VRING_DESC_F_INDIRECT   4   // Buffer contains indirect descriptors

// Virtqueue descriptor
struct virtq_desc {
    uint64_t addr;      // Physical address of buffer
    uint32_t len;       // Length of buffer
    uint16_t flags;     // VRING_DESC_F_*
    uint16_t next;      // Index of next descriptor (if flags & NEXT)
} __attribute__((packed));

// Virtqueue available ring
struct virtq_avail {
    uint16_t flags;
    uint16_t idx;       // Next index to write
    uint16_t ring[];    // Array of descriptor chain heads
} __attribute__((packed));

// Used ring element
struct virtq_used_elem {
    uint32_t id;        // Index of start of used descriptor chain
    uint32_t len;       // Total length written
} __attribute__((packed));

// Virtqueue used ring
struct virtq_used {
    uint16_t flags;
    uint16_t idx;       // Next index device will write to
    struct virtq_used_elem ring[];
} __attribute__((packed));

// Number of descriptors in the queue (must be power of 2)
// Keep small so everything fits in one page for legacy virtio
#define VIRTIO_RING_SIZE 8

// VirtIO block request types
#define VIRTIO_BLK_T_IN         0   // Read
#define VIRTIO_BLK_T_OUT        1   // Write
#define VIRTIO_BLK_T_FLUSH      4   // Flush
#define VIRTIO_BLK_T_DISCARD    11
#define VIRTIO_BLK_T_WRITE_ZEROES 13

// VirtIO block request status
#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

// VirtIO block request header
struct virtio_blk_req {
    uint32_t type;      // VIRTIO_BLK_T_*
    uint32_t reserved;
    uint64_t sector;    // Sector number (512-byte sectors)
} __attribute__((packed));

// Block size
#define SECTOR_SIZE 512
#define BSIZE 1024  // File system block size (can be larger than sector)

// VirtIO block device state
struct virtio_blk {
    volatile uint32_t *regs;    // MMIO base address
    
    // Virtqueue structures (must be page-aligned for device)
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    
    // Track free descriptors
    uint8_t free[VIRTIO_RING_SIZE];     // 1 = free, 0 = in use
    uint16_t used_idx;                   // Last seen used index
    
    // Per-request tracking
    struct {
        struct virtio_blk_req req;
        uint8_t status;
        int done;
    } inflight[VIRTIO_RING_SIZE];
};

// API
void virtio_blk_init(void);
int disk_read(uint64_t sector, void *buf);
int disk_write(uint64_t sector, void *buf);
