#include <stdio.h>

_Atomic int atomic_x = 42;
double _Complex complex_z;

int main() {
    atomic_x = 100;
    if (atomic_x != 100) return 1;

    if (sizeof(complex_z) != 16) return 2;

    unsigned long long *parts = (unsigned long long *)&complex_z;
    parts[0] = 0x4008000000000000ULL;
    parts[1] = 0x4010000000000000ULL;

    if (parts[0] != 0x4008000000000000ULL || parts[1] != 0x4010000000000000ULL) return 3;

    printf("ok complex_atomic: atomic_x = %d, sizeof = %zu\n", atomic_x, sizeof(complex_z));
    return 0;
}
