struct Box {
    int n;
};

int main(void) {
    struct Box box = {3};
    if (--box.n != 2) return 1;
    if (box.n != 2) return 2;
    if (++box.n != 3) return 3;
    return 42;
}
