typedef unsigned int uint32_t;

static uint32_t g = 0xFFFFFFFFU;
static uint32_t *gp = &g;

int main(void) {
    if ((*gp) < 1U) return 1;
    return 42;
}
