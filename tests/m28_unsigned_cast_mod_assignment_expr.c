typedef signed int int32_t;
typedef unsigned int uint32_t;

static int32_t g_13 = -6;
static uint32_t g_15[3];

int main(void) {
    uint32_t *p = &g_15[1];
    int32_t divisor = -9;
    uint32_t r = ((*p) = g_13) % (uint32_t)divisor;
    if (g_15[1] != 4294967290U) return 1;
    if (r != 3U) return 2;
    return 42;
}
