struct S {
    int a;
    int b;
};

int sum(struct S s) {
    return s.a + s.b;
}

int main(void) {
    struct S dst;
    struct S src;
    struct S *p;
    dst.a = 1;
    dst.b = 2;
    src.a = 20;
    src.b = 22;
    p = &dst;
    return sum((*p) = src);
}
