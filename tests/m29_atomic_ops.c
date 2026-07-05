#include <stdio.h>

_Atomic int atomic_counter = 0;
_Atomic int atomic_store_val = 0;

void atomic_increment(void) {
    atomic_counter = atomic_counter + 1;
}

int main() {
    if (sizeof(_Atomic int) != sizeof(int)) return 1;
    if (sizeof(_Atomic long) != sizeof(long)) return 2;

    atomic_store_val = 99;
    if (atomic_store_val != 99) return 3;

    atomic_store_val = atomic_store_val + 1;
    if (atomic_store_val != 100) return 4;

    atomic_counter = 42;
    atomic_increment();
    if (atomic_counter != 43) return 5;

    _Atomic int local_atomic = 7;
    local_atomic = local_atomic * 2;
    if (local_atomic != 14) return 6;

    printf("ok atomic_ops: counter = %d, store = %d\n", atomic_counter, atomic_store_val);
    return 0;
}
