static int g = -8;

static void set_from_compare(int *p) {
    *p = (1 > *p);
}

int main(void) {
    set_from_compare(&g);
    if (g != 1) return 1;
    return 42;
}
