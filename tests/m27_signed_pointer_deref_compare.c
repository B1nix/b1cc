static int g = -10;
static int *gp = &g;

int main(void) {
    if (((0 != gp) ^ (0x60 || 0x59)) < *gp) return 1;
    return 42;
}
