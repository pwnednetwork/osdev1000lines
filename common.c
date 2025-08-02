// ░█▀▀░█▀█░█▄█░█▄█░█▀█░█▀█░░░░█▀▀
// ░█░░░█░█░█░█░█░█░█░█░█░█░░░░█░░
// ░▀▀▀░▀▀▀░▀░▀░▀░▀░▀▀▀░▀░▀░▀░░▀▀▀
// common.c
// supporting functions
#include "common.h"

// memory alloc

paddr_t alloc_pages(uint32_t n) {
  static paddr_t next_paddr = (paddr_t)__free_ram;
  paddr_t paddr = next_paddr;
  next_paddr += n * PAGE_SIZE;

  if (next_paddr > (paddr_t)__free_ram_end)
    PANIC("out of memory");

  memset((void *)paddr, 0, n * PAGE_SIZE);
  return paddr;
} // alloc_pages

// page map
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
  if (!is_aligned(vaddr, PAGE_SIZE))
    PANIC("unaligned vaddr %x", vaddr);

  if (!is_aligned(paddr, PAGE_SIZE))
    PANIC("unaligned paddr %x", paddr);

  uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
  if ((table1[vpn1] & PAGE_V) == 0) {
    // Create the 1st level page table if it doesn't exist.
    uint32_t pt_paddr = alloc_pages(1);
    table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
  }

  // Set the 2nd level page table entry to map the physical page.
  uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
  uint32_t *table0 = (uint32_t *)((table1[vpn1] >> 10) * PAGE_SIZE);
  table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
} // map_page

// copies n bytes from src->dst
void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  while (n--)
    *d++ = *s++;
  return dst;
}

// set memory starting at buf to value c until buf+n
// buf[cccccccccccccccc]buf+n
void *memset(void *buf, char c, size_t n) {
  uint8_t *p = (uint8_t *)buf;
  while (n--)
    *p++ = c;
  return buf;
}

// copies string from src to dst
// keeps copying until *src=='\0'
// returns dst
// VERY UNSAFE (use strcpy_s instead)
char *strcpy(char *dst, const char *src) {
  char *d = dst;
  while (*src)
    *d++ = *src++;
  *d = '\0';
  return dst;
}

// compares two strings s1 and s2
// s1 and s2 are identical when strcmp returns error
// s1 == s2 --> 0
// s1 > s2 --> positive value
// s1 < s2 --> negative value
int strcmp(const char *s1, const char *s2) {
  while (*s1 && *s2) {
    if (*s1 != *s2)
      break;
    s1++;
    s2++;
  }

  // pointer math
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

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

// putting one char to the screen using sbi_call primitive
// extension: Console Putchar (EID 0x01)
//
void putchar(char ch) {
  sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}

void printf(const char *fmt, ...) {
  va_list vargs;
  va_start(vargs, fmt);

  while (*fmt) {
    if (*fmt == '%') {
      fmt++;          // Skip '%'
      switch (*fmt) { // Read the next character
      case '\0':      // '%' at the end of the format string
        putchar('%');
        goto end;
      case '%': // Print '%'
        putchar('%');
        break;
      case 's': { // Print a NULL-terminated string.
        const char *s = va_arg(vargs, const char *);
        while (*s) {
          putchar(*s);
          s++;
        }
        break;
      }
      case 'd': { // Print an integer in decimal.
        int value = va_arg(vargs, int);
        unsigned magnitude =
            value; // https://github.com/nuta/operating-system-in-1000-lines/issues/64
        if (value < 0) {
          putchar('-');
          magnitude = -magnitude;
        }

        unsigned divisor = 1;
        while (magnitude / divisor > 9)
          divisor *= 10;

        while (divisor > 0) {
          putchar('0' + magnitude / divisor);
          magnitude %= divisor;
          divisor /= 10;
        }

        break;
      }
      case 'x': { // Print an integer in hexadecimal.
        unsigned value = va_arg(vargs, unsigned);
        for (int i = 7; i >= 0; i--) {
          unsigned nibble = (value >> (i * 4)) & 0xf;
          putchar("0123456789abcdef"[nibble]);
        }
      }
      }
    } else {
      putchar(*fmt);
    }

    fmt++;
  }

end:
  va_end(vargs);
} // printf
