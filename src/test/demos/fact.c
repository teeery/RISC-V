/* fact.c — 递归阶乘 */
#include "systub.h"

static int factorial(int n)
{
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

void _start(void)
{
    char buf[16];

    print("Factorial (1..10):\n");
    for (int i = 1; i <= 10; i++) {
        print(itoa(i, buf));
        print("! = ");
        print(itoa(factorial(i), buf));
        print("\n");
    }
    sys_exit(0);
}
