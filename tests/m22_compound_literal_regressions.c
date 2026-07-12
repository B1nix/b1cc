/* M34: array and positional struct compound literals are real objects. */
struct Pair {
    int a;
    int b;
};

int main(void) {
    int *p = (int[]){2, 4, 6};
    if (p[0] != 2 || p[1] != 4 || p[2] != 6) return 1;
    if ((int[]){10, 20, 30}[1] != 20) return 2;

    struct Pair s = (struct Pair){3, 4};
    if (s.a != 3 || s.b != 4) return 3;
    return 42;
}
