enum {
    n = 7,
};

int main(void) {
    int n = 3;
    if (--n != 2) return 1;
    return n == 2 ? 42 : 2;
}
