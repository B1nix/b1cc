/* tests/m15_abi_returns.c — test small integer return truncation */
char get_char(int x) {
    return (char)x;
}

short get_short(int x) {
    return (short)x;
}

int main(void) {
    if (get_char(257) != 1) return 1;    /* 257 & 0xFF = 1 */
    if (get_short(65537) != 1) return 2; /* 65537 & 0xFFFF = 1 */
    return 0;
}
