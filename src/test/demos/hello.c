/* hello.c — Hello World */
#include "systub.h"

void _start(void)
{
    print("Hello, World!\n");
    sys_exit(0);
}
