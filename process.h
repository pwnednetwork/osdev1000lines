// ░█▀█░█▀▄░█▀█░█▀▀░█▀▀░█▀▀░█▀▀░░░░█░█
// ░█▀▀░█▀▄░█░█░█░░░█▀▀░▀▀█░▀▀█░░░░█▀█
// ░▀░░░▀░▀░▀▀▀░▀▀▀░▀▀▀░▀▀▀░▀▀▀░▀░░▀░▀
//

#ifndef PROCESS_H_
#define PROCESS_H_

#include "common.h"

// setting exception pin when going to u-mode
#define SSTATUS_SPIE (1 << 5)

/*---------------- process ------------------------------------------------*/

#define PROCS_MAX 8 // maximum number of processes

#define PROC_UNUSED 0   // unused process control structure
#define PROC_RUNNABLE 1 // runnable process

#define USER_BASE 0x1000000
struct process {
  int pid;    // process ID
  int state;  // process state: PROC_UNUSED or PROC_RUNNABLE
              // __attribute__((naked)) void switch_context(uint32_t *prev_sp /*
              // a0  */,
  vaddr_t sp; // stack pointer
  uint32_t *page_table; // page table
  uint8_t stack[8192];  // kernel stack uint32_t *next_sp /* a1 */);
};

// globals

extern struct process procs[PROCS_MAX]; // global process list

struct process *create_process(const void *image, size_t image_size);

extern struct process *current_proc;
extern struct process *idle_proc; // Idle process

// functions

void yield(void);

#endif // PROCESS_H_
