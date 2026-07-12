int main(void) {
    int x = 2147483647;
    int y = x + 1;
    if (y != -2147483648) return 1;
    int z = -2147483648;
    int w = z - 1;
    if (w != 2147483647) return 2;
    int a = 2147483647;
    int b = a * 2;
    if (b != -2) return 3;
    return 0;
}
