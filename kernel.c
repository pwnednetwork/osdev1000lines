#include "kernel.h"
#include "common.h"
#include "filesystem.h"
#include "process.h"

// bunch of externs to work with memory
extern char __kernel_base[];
extern char __stack_top[];
extern char __bss[], __bss_end[];
extern char __free_ram[], __free_ram_end[];
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];

extern struct process procs[PROCS_MAX];
extern struct process *current_proc;
extern struct process *idle_proc;

// ┌────────────────────────────────────────────────────────────────────────────
// │
// │
//
//      virtio functions │
//                                      ────────────────────────────────────────┘

//////////////////////////////////////////////////////////////
// functions for accessing MMIO
//
uint32_t virtio_reg_read32(unsigned offset) {
  return *((volatile uint32_t *)(VIRTIO_BLK_PADDR + offset));
}

uint64_t virtio_reg_read64(unsigned offset) {
  return *((volatile uint64_t *)(VIRTIO_BLK_PADDR + offset));
}

void virtio_reg_write32(unsigned offset, uint32_t value) {
  *((volatile uint32_t *)(VIRTIO_BLK_PADDR + offset)) = value;
}

void virtio_reg_fetch_and_or32(unsigned offset, uint32_t value) {
  virtio_reg_write32(offset, virtio_reg_read32(offset) | value);
}

// virtio structures
struct virtio_virtq *blk_request_vq;
struct virtio_blk_req *blk_req;
paddr_t blk_req_paddr;
uint64_t blk_capacity;

// virtqueu init
struct virtio_virtq *virtq_init(unsigned index) {
  // Allocate a region for the virtqueue.
  paddr_t virtq_paddr =
      alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
  struct virtio_virtq *vq = (struct virtio_virtq *)virtq_paddr;
  vq->queue_index = index;
  vq->used_index = (volatile uint16_t *)&vq->used.index;
  // 1. Select the queue writing its index (first queue is 0) to QueueSel.
  virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
  // 5. Notify the device about the queue size by writing the size to QueueNum.
  virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
  // 6. Notify the device about the used alignment by writing its value in bytes
  // to QueueAlign.
  virtio_reg_write32(VIRTIO_REG_QUEUE_ALIGN, 0);
  // 7. Write the physical number of the first page of the queue to the QueuePFN
  // register.
  virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr);
  return vq;
}

// virtio init
void virtio_blk_init(void) {
  if (virtio_reg_read32(VIRTIO_REG_MAGIC) != 0x74726976)
    PANIC("virtio: invalid magic value");
  if (virtio_reg_read32(VIRTIO_REG_VERSION) != 1)
    PANIC("virtio: invalid version");
  if (virtio_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK)
    PANIC("virtio: invalid device id");

  // 1. Reset the device.
  virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
  // 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
  virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
  // 3. Set the DRIVER status bit.
  virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
  // 5. Set the FEATURES_OK status bit.
  virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FEAT_OK);
  // 7. Perform device-specific setup, including discovery of virtqueues for the
  // device
  blk_request_vq = virtq_init(0);
  // 8. Set the DRIVER_OK status bit.
  virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

  // Get the disk capacity.
  blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
  printf("virtio-blk: capacity is %d bytes\n", blk_capacity);

  // Allocate a region to store requests to the device.
  blk_req_paddr =
      alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
  blk_req = (struct virtio_blk_req *)blk_req_paddr;
}

// Notifies the device that there is a new request. `desc_index` is the index
// of the head descriptor of the new request.
void virtq_kick(struct virtio_virtq *vq, int desc_index) {
  vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
  vq->avail.index++;
  __sync_synchronize();
  virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
  vq->last_used_index++;
}

// Returns whether there are requests being processed by the device.
bool virtq_is_busy(struct virtio_virtq *vq) {
  return vq->last_used_index != *vq->used_index;
}

// ┌────────────────────────────────────────────────────────────────────────────
// │
// │
//
//        "tar" filesystem stuff │
//                                      ────────────────────────────────────────┘

// Reads/writes from/to virtio-blk device.
void read_write_disk(void *buf, unsigned sector, int is_write) {
  if (sector >= blk_capacity / SECTOR_SIZE) {
    printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
           sector, blk_capacity / SECTOR_SIZE);
    return;
  }

  // Construct the request according to the virtio-blk specification.
  blk_req->sector = sector;
  blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  if (is_write)
    memcpy(blk_req->data, buf, SECTOR_SIZE);

  // Construct the virtqueue descriptors (using 3 descriptors).
  struct virtio_virtq *vq = blk_request_vq;
  vq->descs[0].addr = blk_req_paddr;
  vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
  vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
  vq->descs[0].next = 1;

  vq->descs[1].addr = blk_req_paddr + offsetof(struct virtio_blk_req, data);
  vq->descs[1].len = SECTOR_SIZE;
  vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
  vq->descs[1].next = 2;

  vq->descs[2].addr = blk_req_paddr + offsetof(struct virtio_blk_req, status);
  vq->descs[2].len = sizeof(uint8_t);
  vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

  // Notify the device that there is a new request.
  virtq_kick(vq, 0);

  // Wait until the device finishes processing.
  while (virtq_is_busy(vq))
    ;

  // virtio-blk: If a non-zero value is returned, it's an error.
  if (blk_req->status != 0) {
    printf("virtio: warn: failed to read/write sector=%d status=%d\n", sector,
           blk_req->status);
    return;
  }

  // For read operations, copy the data into the buffer.
  if (!is_write)
    memcpy(buf, blk_req->data, SECTOR_SIZE);
}

struct file files[FILES_MAX];
uint8_t disk[DISK_MAX_SIZE];

int oct2int(char *oct, int len) {
  int dec = 0;
  for (int i = 0; i < len; i++) {
    if (oct[i] < '0' || oct[i] > '7')
      break;

    dec = dec * 8 + (oct[i] - '0');
  }
  return dec;
}

void fs_init(void) {
  for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
    read_write_disk(&disk[sector * SECTOR_SIZE], sector, false);

  unsigned off = 0;
  for (int i = 0; i < FILES_MAX; i++) {
    struct tar_header *header = (struct tar_header *)&disk[off];
    if (header->name[0] == '\0')
      break;

    if (strcmp(header->magic, "ustar") != 0)
      PANIC("invalid tar header: magic=\"%s\"", header->magic);

    int filesz = oct2int(header->size, sizeof(header->size));
    struct file *file = &files[i];
    file->in_use = true;
    strcpy(file->name, header->name);
    memcpy(file->data, header->data, filesz);
    file->size = filesz;
    printf("file: %s, size=%d\n", file->name, file->size);

    off += align_up(sizeof(struct tar_header) + filesz, SECTOR_SIZE);
  }
} // fs_init

// fs_flush: write all files to disk using read_write_disk
void fs_flush(void) {
  // copy all file contents into `disk` buffer.
  memset(disk, 0, sizeof(disk));
  unsigned off = 0;
  for (int file_i = 0; file_i < FILES_MAX; file_i++) {
    struct file *file = &files[file_i];
    if (!file->in_use)
      continue;

    struct tar_header *header = (struct tar_header *)&disk[off];
    memset(header, 0, sizeof(*header));
    strcpy(header->name, file->name);
    strcpy(header->mode, "000644");
    strcpy(header->magic, "ustar");
    strcpy(header->version, "00");
    header->type = '0';

    // turn the file size into an octal string.
    int filesz = file->size;
    for (int i = sizeof(header->size); i > 0; i--) {
      header->size[i - 1] = (filesz % 8) + '0';
      filesz /= 8;
    }

    // calculate the checksum.
    int checksum = ' ' * sizeof(header->checksum);
    for (unsigned i = 0; i < sizeof(struct tar_header); i++)
      checksum += (unsigned char)disk[off + i];

    for (int i = 5; i >= 0; i--) {
      header->checksum[i] = (checksum % 8) + '0';
      checksum /= 8;
    }

    // copy file data.
    memcpy(header->data, file->data, file->size);
    off += align_up(sizeof(struct tar_header) + file->size, SECTOR_SIZE);
  }

  // Write `disk` buffer into the virtio-blk.
  for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
    read_write_disk(&disk[sector * SECTOR_SIZE], sector, true);

  printf("wrote %d bytes to disk\n", sizeof(disk));
} // fs_flush

////////////////////////////////////////////////////////////////////

// a call to OpenSBI
// pass:
// arg0..arg5 are arguments
// fid is SBI function id
// eid is SBI extension id
// return:
// SBI functions return in a0 and a1, with a0 being error code
// sbi_call wraps that into a struct sbiret (sbi return)
// See:
//     opensbi/lib/sbi/sbi_ecall_legacy.c function sbi_ecall_legacy_handler()
struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid) {

  // binding registers to variables
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  register long a3 __asm__("a3") = arg3;
  register long a4 __asm__("a4") = arg4;
  register long a5 __asm__("a5") = arg5;
  register long a6 __asm__("a6") = fid;
  register long a7 __asm__("a7") = eid;

  // ecall is riscv-32 asm call from kernel to SBI
  // 1. changes to supervisor mode
  // 2. jumps to stvec location
  __asm__ __volatile__("ecall"
                       : "=r"(a0), "=r"(a1)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                         "r"(a6), "r"(a7)
                       : "memory");
  return (struct sbiret){.error = a0, .value = a1};
}

// allocate next page and zero it out
paddr_t alloc_pages(uint32_t n) {
  static paddr_t next_paddr = (paddr_t)__free_ram;
  paddr_t paddr = next_paddr;
  next_paddr += n * PAGE_SIZE;

  if (next_paddr > (paddr_t)__free_ram_end)
    PANIC("out of memory");

  memset((void *)paddr, 0, n * PAGE_SIZE);
  return paddr;
}

// map pages using riscv's Sv32's page table
// vpn: virtual page number
// pfn: physical frame number
// pages are virtual, frames are physical
// Sv32 uses two-level page table
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
  if (!is_aligned(vaddr, PAGE_SIZE))
    PANIC("unaligned vaddr %x", vaddr);

  if (!is_aligned(paddr, PAGE_SIZE))
    PANIC("unaligned paddr %x", paddr);

  uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
  if ((table1[vpn1] & PAGE_V) == 0) {
    uint32_t pt_paddr = alloc_pages(1);
    table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
  }

  uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
  uint32_t *table0 = (uint32_t *)((table1[vpn1] >> 10) * PAGE_SIZE);
  table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}

// putchar using riscv's shenanigans hidden awayin sbi_call
void putchar(char ch) { sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* putchar */); }

// same as putchar, except we are getting something out
long getchar(void) {
  struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2 /* getchar */);
  return ret.error;
}

__attribute__((naked)) __attribute__((aligned(4))) void kernel_entry(void) {
  __asm__ __volatile__("csrrw sp, sscratch, sp\n"
                       "addi sp, sp, -4 * 31\n"
                       "sw ra,  4 * 0(sp)\n"
                       "sw gp,  4 * 1(sp)\n"
                       "sw tp,  4 * 2(sp)\n"
                       "sw t0,  4 * 3(sp)\n"
                       "sw t1,  4 * 4(sp)\n"
                       "sw t2,  4 * 5(sp)\n"
                       "sw t3,  4 * 6(sp)\n"
                       "sw t4,  4 * 7(sp)\n"
                       "sw t5,  4 * 8(sp)\n"
                       "sw t6,  4 * 9(sp)\n"
                       "sw a0,  4 * 10(sp)\n"
                       "sw a1,  4 * 11(sp)\n"
                       "sw a2,  4 * 12(sp)\n"
                       "sw a3,  4 * 13(sp)\n"
                       "sw a4,  4 * 14(sp)\n"
                       "sw a5,  4 * 15(sp)\n"
                       "sw a6,  4 * 16(sp)\n"
                       "sw a7,  4 * 17(sp)\n"
                       "sw s0,  4 * 18(sp)\n"
                       "sw s1,  4 * 19(sp)\n"
                       "sw s2,  4 * 20(sp)\n"
                       "sw s3,  4 * 21(sp)\n"
                       "sw s4,  4 * 22(sp)\n"
                       "sw s5,  4 * 23(sp)\n"
                       "sw s6,  4 * 24(sp)\n"
                       "sw s7,  4 * 25(sp)\n"
                       "sw s8,  4 * 26(sp)\n"
                       "sw s9,  4 * 27(sp)\n"
                       "sw s10, 4 * 28(sp)\n"
                       "sw s11, 4 * 29(sp)\n"

                       "csrr a0, sscratch\n"
                       "sw a0,  4 * 30(sp)\n"

                       "addi a0, sp, 4 * 31\n"
                       "csrw sscratch, a0\n"

                       "mv a0, sp\n"
                       "call handle_trap\n"

                       "lw ra,  4 * 0(sp)\n"
                       "lw gp,  4 * 1(sp)\n"
                       "lw tp,  4 * 2(sp)\n"
                       "lw t0,  4 * 3(sp)\n"
                       "lw t1,  4 * 4(sp)\n"
                       "lw t2,  4 * 5(sp)\n"
                       "lw t3,  4 * 6(sp)\n"
                       "lw t4,  4 * 7(sp)\n"
                       "lw t5,  4 * 8(sp)\n"
                       "lw t6,  4 * 9(sp)\n"
                       "lw a0,  4 * 10(sp)\n"
                       "lw a1,  4 * 11(sp)\n"
                       "lw a2,  4 * 12(sp)\n"
                       "lw a3,  4 * 13(sp)\n"
                       "lw a4,  4 * 14(sp)\n"
                       "lw a5,  4 * 15(sp)\n"
                       "lw a6,  4 * 16(sp)\n"
                       "lw a7,  4 * 17(sp)\n"
                       "lw s0,  4 * 18(sp)\n"
                       "lw s1,  4 * 19(sp)\n"
                       "lw s2,  4 * 20(sp)\n"
                       "lw s3,  4 * 21(sp)\n"
                       "lw s4,  4 * 22(sp)\n"
                       "lw s5,  4 * 23(sp)\n"
                       "lw s6,  4 * 24(sp)\n"
                       "lw s7,  4 * 25(sp)\n"
                       "lw s8,  4 * 26(sp)\n"
                       "lw s9,  4 * 27(sp)\n"
                       "lw s10, 4 * 28(sp)\n"
                       "lw s11, 4 * 29(sp)\n"
                       "lw sp,  4 * 30(sp)\n"
                       "sret\n");
}

// handle_syscall is essentially one big switch statement that
// selects what the syscall is and handles it
// the syscall number itself is in f->a3, the data is in f->a0
void handle_syscall(struct trap_frame *f) {
  switch (f->a3) {
  case SYS_PUTCHAR:
    putchar(f->a0);
    break;
  case SYS_GETCHAR:
    while (1) {
      long ch = getchar();
      if (ch >= 0) {
        f->a0 = ch;
        break;
      }

      yield();
    }
    break;
  case SYS_EXIT:
    printf("process %d exited\n", current_proc->pid);
    current_proc->state = PROC_EXITED;
    yield();
    PANIC("unreachable");

  default:
    PANIC("unexpected syscall a3=%x\n", f->a3);
  }
}

// handle traps including syscalls using trap_frame
void handle_trap(struct trap_frame *f) {
  uint32_t scause = READ_CSR(scause);
  uint32_t stval = READ_CSR(stval);
  uint32_t user_pc = READ_CSR(sepc);
  if (scause == SCAUSE_ECALL) {
    handle_syscall(f);
    user_pc += 4;
  } else {
    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval,
          user_pc);
  }

  WRITE_CSR(sepc, user_pc);
}

// boot jumps here
void kernel_main(void) {
  memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
  printf("\n\n");
  WRITE_CSR(stvec, (uint32_t)kernel_entry);

  // init virtio
  virtio_blk_init();
  // init fs
  fs_init();
  char buf[SECTOR_SIZE];
  read_write_disk(buf, 0, false /* read from the disk */);
  printf("first sector: %s\n", buf);

  strcpy(buf, "hello from kernel!!!\n");
  read_write_disk(buf, 0, true /* write to the disk */);

  idle_proc = create_process(NULL, 0);
  idle_proc->pid = 0; // idle
  current_proc = idle_proc;

  create_process(_binary_shell_bin_start, (size_t)_binary_shell_bin_size);
  yield();

  PANIC("switched to idle process");
}

// main booting function
__attribute__((section(".text.boot"))) __attribute__((naked)) void boot(void) {
  __asm__ __volatile__("mv sp, %[stack_top]\n"
                       "j kernel_main\n"
                       :
                       : [stack_top] "r"(__stack_top));
}
