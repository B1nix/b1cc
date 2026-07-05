#include <stdio.h>

_Alignas(64) int global_x = 42;
_Alignas(32) char global_y = 100;

int main() {
    if (((unsigned long)&global_x % 64) != 0) return 1;
    if (((unsigned long)&global_y % 32) != 0) return 2;

    printf("ok alignas: global_x = %d, global_y = %d\n", global_x, global_y);
    return 0;
}
