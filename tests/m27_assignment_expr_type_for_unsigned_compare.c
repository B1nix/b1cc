int main(void) {
    int s = (int)0xC21B07FB;
    if ((s |= 0) < 1U) return 1;
    return 42;
}
