// ░█▀▀░█▀█░█▄█░█▄█░█▀█░█▀█░░░█░█
// ░█░░░█░█░█░█░█░█░█░█░█░█░░░█▀█
// ░▀▀▀░▀▀▀░▀░▀░▀░▀░▀▀▀░▀░▀░▀░▀░▀

// common.h
#pragma once

// types

typedef int bool;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef uint32_t size_t;
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

// syscalls

#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2
#define SYS_EXIT 3
// globals

extern char __free_ram[], __free_ram_end[];

// page table macros
#define SATP_SV32 (1u << 31)
#define PAGE_V (1 << 0) // "Valid" bit (entry is enabled)
#define PAGE_R (1 << 1) // Readable
#define PAGE_W (1 << 2) // Writable
#define PAGE_X (1 << 3) // Executable
#define PAGE_U (1 << 4) // User (accessible in user mode)

// other macros

#define true 1
#define false 0
#define NULL ((void *)0)
#define align_up(value, align) __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)
#define offsetof(type, member) __builtin_offsetof(type, member)
#define va_list __builtin_va_list
#define va_start __builtin_va_start
#define va_end __builtin_va_end
#define va_arg __builtin_va_arg

#define PANIC(fmt, ...)                                                        \
  do {                                                                         \
    printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);      \
    while (1) {                                                                \
    }                                                                          \
  } while (0)

// structures

// struct sbiret {
//   long error;
//   long value;
// };

// memory
#define PAGE_SIZE 4096

/*---------- functions-------------------------------------------------------*/

// page_table
extern paddr_t alloc_pages(uint32_t n);
//
extern void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t);
// flags);

// sets an area of memory to a certain character c
void *memset(void *buf, char c, size_t n);
// copies from dst to src of size n, unsafe
void *memcpy(void *dst, const void *src, size_t n);
// copies string from src to dst, definitely unsafe
char *strcpy(char *dst, const char *src);
// compares s1 to s2
int strcmp(const char *s1, const char *s2);

// sbi call wraper
// struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
//                        long arg5, long fid, long eid);

// some functions for text output
void putchar(char ch);
void printf(const char *fmt, ...);
