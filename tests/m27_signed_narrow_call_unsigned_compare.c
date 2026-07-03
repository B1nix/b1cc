static short narrow_negative(void) {
    return -18537;
}

int main(void) {
    if (0xD714F98C > narrow_negative()) return 1;
    return 42;
}
