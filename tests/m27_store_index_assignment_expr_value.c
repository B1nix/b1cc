union U {
    int f0;
};

int g = -8;
union U src[1] = {{0xF6802998}};

int main(void) {
    union U p = {0};
    g ^= (p.f0 = src[0].f0);
    if (p.f0 != (int)0xF6802998U) return 1;
    if (g != 159372896) return 2;
    return 42;
}
