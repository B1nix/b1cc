/* tests/m22_anonymous_union_scalar_init.c - Nested braces around anonymous union scalar fields. */

typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

typedef struct Operand {
    uint32_t type;
    union {
        uint8_t reg;
        uint32_t regset;
    };
} Operand;

static const Operand zero = { 7, { 0 } };
static const Operand one = { 8, { 1 } };

int main(void) {
    if (zero.type != 7) return 1;
    if (zero.reg != 0) return 2;
    if (one.type != 8) return 3;
    if (one.reg != 1) return 4;
    return 42;
}
