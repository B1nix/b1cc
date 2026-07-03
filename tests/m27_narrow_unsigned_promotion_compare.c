typedef unsigned short uint16_t;
typedef signed char int8_t;

int main(void) {
    uint16_t u = 60000;
    int8_t s = -31;
    if (u < s) return 1;
    return 42;
}
