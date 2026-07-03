typedef signed char int8_t;

static int8_t g8 = (int8_t)0x82;
static int g32 = 0;

int main(void) {
    int8_t *p = &g8;
    g32 = ((*p) &= 0xED);
    if (g8 != -128) {
        return 1;
    }
    if (g32 != -128) {
        return 2;
    }
    return 42;
}
