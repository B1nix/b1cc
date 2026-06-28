/* tests/m15_abi_stack_args.c — test stack-passed arguments */
int sum10(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
    return a + b + c + d + e + f + g + h + i + j;
}

int main(void) {
    int res = sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    if (res != 55) {
        return 1;
    }
    return 0;
}
