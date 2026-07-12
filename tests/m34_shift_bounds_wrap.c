int shift_left(int x, int s) {
    return x << s;
}
int shift_right(int x, int s) {
    return x >> s;
}
int main(void) {
    if (shift_left(1, 32) != 0) return 1;
    if (shift_left(1, 33) != 0) return 2;
    if (shift_left(1, 64) != 1) return 3;
    if (shift_right(256, 32) != 0) return 4;
    if (shift_left(1, 0) != 1) return 5;
    if (shift_left(1, -1) != 0) return 6;
    return 0;
}
