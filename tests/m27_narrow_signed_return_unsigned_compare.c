typedef unsigned int uint32_t;
typedef short int16_t;

static uint32_t g = 0xFFFFFFFFU;

static int16_t f(void) {
    return g;
}

int main(void) {
    int x = (f() <= g);
    if (x != 1) return 1;
    return 42;
}
