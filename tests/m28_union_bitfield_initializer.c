union U {
    volatile unsigned f0 : 18;
};

static union U values[2] = {{0x1a13c53dL}, {0x1a13c53dL}};

int main(void) {
    if (values[0].f0 != 247101U) return 1;
    if (values[1].f0 != 247101U) return 2;
    return 42;
}
