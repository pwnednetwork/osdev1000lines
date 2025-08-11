// ░█▀▀░█░█░█▀▀░█░░░█░░░░░░█▀▀
// ░▀▀█░█▀█░█▀▀░█░░░█░░░░░░█░░
// ░▀▀▀░▀░▀░▀▀▀░▀▀▀░▀▀▀░▀░░▀▀▀
// shell.c

#include "user.h"

// main function of shell
// for now we just test things with a forced page fault
void main(void) {
    *((volatile int *)0x80200000) = 0x1234;
    for (;;)
        ;
}
