/* tests/m15_abi_func_ptr.c — test indirect function pointer calls */
int add(int a, int b) {
    return a + b;
}

int main(void) {
    int (*fn)(int, int) = add;
    if (fn(10, 20) != 30) return 1;
    if ((*fn)(5, 5) != 10) return 2;
    return 0;
}
