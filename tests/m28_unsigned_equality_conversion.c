int main(void) {
    unsigned int u = 4294967287U;
    int s = -9;

    if (u != s) return 1;
    if (!(u == s)) return 2;
    return 42;
}
