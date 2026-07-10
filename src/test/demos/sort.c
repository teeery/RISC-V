/* sort.c — 冒泡排序 */
#include "systub.h"

static int arr[] = {64, 34, 25, 12, 22, 11, 90, 45, 78, 3};
static int n = 10;

void _start(void)
{
    char buf[16];

    print("Before: ");
    for (int i = 0; i < n; i++) {
        print(itoa(arr[i], buf));
        if (i < n - 1) print(", ");
    }
    print("\n");

    /* Bubble sort */
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            if (arr[j] > arr[j + 1]) {
                int t = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = t;
            }
        }
    }

    print("After:  ");
    for (int i = 0; i < n; i++) {
        print(itoa(arr[i], buf));
        if (i < n - 1) print(", ");
    }
    print("\n");
    sys_exit(0);
}
