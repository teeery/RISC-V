/* fib.c — 斐波那契数列（前 20 项） */
#include "systub.h"

void _start(void)
{
    int a = 0, b = 1;
    char buf[16];

    print("Fibonacci (first 20):\n");

    for (int i = 0; i < 20; i++) {
        print(itoa(a, buf));
        if (i < 19) print(", ");
        int next = a + b;
        a = b;
        b = next;
    }
    print("\n");
    sys_exit(0);
}
