/* tests/m14_sizeof.c — Test sizeof operator */
int main(void) {
    if (sizeof(char) != 1) return 1;
    if (sizeof(short) != 2) return 2;
    if (sizeof(int) != 4) return 3;
    if (sizeof(long) != 8) return 4;
    return 0;
}
