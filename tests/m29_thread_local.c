#include <stdio.h>

_Thread_local int thread_val = 123;
thread_local double thread_double = 3.14;

int main() {
    if (thread_val != 123) return 1;
    if (thread_double != 3.14) return 2;
    thread_val = 456;
    thread_double = 2.718;
    if (thread_val != 456) return 3;
    if (thread_double != 2.718) return 4;
    
    printf("ok thread_local: thread_val = %d\n", thread_val);
    return 0;
}
