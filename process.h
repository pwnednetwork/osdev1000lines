// ░█▀█░█▀▄░█▀█░█▀▀░█▀▀░█▀▀░█▀▀░░░░█░█
// ░█▀▀░█▀▄░█░█░█░░░█▀▀░▀▀█░▀▀█░░░░█▀█
// ░▀░░░▀░▀░▀▀▀░▀▀▀░▀▀▀░▀▀▀░▀▀▀░▀░░▀░▀
//

#ifndef PROCESS_H_
#define PROCESS_H_

#include "common.h"

/*---------------- process ------------------------------------------------*/

#define PROCS_MAX 8 // Maximum number of processes

#define PROC_UNUSED 0   // Unused process control structure
#define PROC_RUNNABLE 1 // Runnable process

struct process {
  int pid;    // Process ID
  int state;  // Process state: PROC_UNUSED or PROC_RUNNABLE
              // __attribute__((naked)) void switch_context(uint32_t *prev_sp /*
              // a0  */,
  vaddr_t sp; // Stack pointer
  uint8_t stack[8192]; // Kernel stack uint32_t *next_sp /* a1 */);
};

extern struct process procs[PROCS_MAX];
struct process *create_process(uint32_t pc);

extern struct process *current_proc;
extern struct process *idle_proc; // Idle process

void yield(void);

#endif // PROCESS_H_
