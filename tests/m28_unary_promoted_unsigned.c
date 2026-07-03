typedef unsigned short uint16_t;

static uint16_t g = 0xedc7;

int main(void) {
    int branch = 0;

    if ((~g) >= 0) {
        branch = 1;
    }

    return branch;
}
