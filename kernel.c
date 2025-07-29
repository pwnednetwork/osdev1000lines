// ░█░█░█▀▀░█▀▄░█▀█░█▀▀░█░░░░░█▀▀
// ░█▀▄░█▀▀░█▀▄░█░█░█▀▀░█░░░░░█░░
// ░▀░▀░▀▀▀░▀░▀░▀░▀░▀▀▀░▀▀▀░▀░▀▀▀
// kernel.c
// main file for the kernel: bootloader and essentials

#include "kernel.h"
#include "process.h"

// handle trap
void handle_trap(struct trap_frame *f) {
  uint32_t scause = READ_CSR(scause);
  uint32_t stval = READ_CSR(stval);
  uint32_t user_pc = READ_CSR(sepc);

  PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval,
        user_pc);
}

// kernel entry
__attribute__((naked)) __attribute__((aligned(4))) void kernel_entry(void) {
  __asm__ __volatile__("csrw sscratch, sp\n"
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
                       "sw a0, 4 * 30(sp)\n"

                       // Reset the kernel stack.
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
} // kernel_entry

// memory alloc
extern char __free_ram[], __free_ram_end[];

paddr_t alloc_pages(uint32_t n) {
  static paddr_t next_paddr = (paddr_t)__free_ram;
  paddr_t paddr = next_paddr;
  next_paddr += n * PAGE_SIZE;

  if (next_paddr > (paddr_t)__free_ram_end)
    PANIC("out of memory");

  memset((void *)paddr, 0, n * PAGE_SIZE);
  return paddr;
} // alloc_pages

// sections of memory and elf file
extern char __bss[], __bss_end[], __stack_top[];

void exception_test() {
  WRITE_CSR(stvec, (uint32_t)kernel_entry); // exception test
  __asm__ __volatile__("unimp");            // exception test
}

void memory_test() {

  printf("memory_test():\n");
  paddr_t paddr0 = alloc_pages(32);
  paddr_t paddr1 = alloc_pages(1);
  printf("---- alloc_pages test: paddr0=%x\n", paddr0);
  printf("---- alloc_pages test: paddr1=%x\n", paddr1);
  printf("---- __free_ram=%x\n", __free_ram);

  printf("\n---- testing __free_ram==paddr0\n");
  if (paddr0 != __free_ram) {
    PANIC("__free_ram!=paddr0");
  }
  printf("---- test succeeded\n");
}

/* ----------- test context switch --------------- */

void delay(void) {
  for (int i = 0; i < 30000000; i++)
    __asm__ __volatile__("nop"); // do nothing
} // delay

struct process *proc_a;
struct process *proc_b;

void proc_a_entry(void) {
  printf("starting process A\n");
  while (1) {

    putchar('A');
    //    switch_context(&proc_a->sp, &proc_b->sp);
    //   delay();
    yield();
  }
} // void proc_a_entry

void proc_b_entry(void) {
  printf("starting process B\n");
  while (1) {
    putchar('B');
    //    switch_context(&proc_b->sp, &proc_a->sp);
    //   delay();
    yield();
  }
} // void proc_b_entry

// small test to test process
//
void test_proc()

{
  idle_proc = create_process((uint32_t)NULL);
  idle_proc->pid = 0; // idle
  current_proc = idle_proc;

  proc_a = create_process((uint32_t)proc_a_entry);
  proc_b = create_process((uint32_t)proc_b_entry);
  // proc_a_entry();
  yield();
} // void test_proc()

// like main but for operating system's kernel

void kernel_main(void) {

  ////////////////////////////////////////////////////////
  ///////// setup
  // size of bss via some basic pointer math
  size_t bss_size = (size_t)__bss_end - (size_t)__bss;
  // setting bss section with all zeroes
  memset(__bss, 0, bss_size);
  const char *s = "kernel_main() loading \n";
  printf("%s", s);

  ////////////////////////////////////////////////////////
  ///////// tests
  memory_test();
  test_proc();
  printf("kernel_main(): booted\n");

  ////////////////////////////////////////////////////////

  // infinite loop
  for (;;) {
    __asm__ __volatile__("wfi");
  }

} // void kernel_main()

// this one kicks off everything and (j)umps to kernel_main

__attribute__((section(".text.boot"))) __attribute__((naked)) void boot(void) {
  __asm__ __volatile__(
      "mv sp, %[stack_top]\n" // set the stack pointer
      "j kernel_main\n"       // jump to the kernel main function
      :
      : [stack_top] "r"(
          __stack_top) // pass the stack top address as %[stack_top]
  );
} // void boot()
