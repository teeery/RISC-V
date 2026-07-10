/* prime.c — 质数判定 */
#include "systub.h"

static int is_prime(int n)
{
    if (n < 2) return 0;
    for (int i = 2; i * i <= n; i++) {
        if (n % i == 0) return 0;
    }
    return 1;
}

void _start(void)
{
    char buf[16];
    int count = 0;

    print("Primes under 100:\n");
    for (int i = 2; i < 100; i++) {
        if (is_prime(i)) {
            if (count > 0) print(", ");
            print(itoa(i, buf));
            count++;
        }
    }
    print("\n");
    print("Count: ");
    print(itoa(count, buf));
    print("\n");
    sys_exit(0);
}
