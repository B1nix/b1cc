/* tests/m22_enum_const_expr.c - Enum initializers accept constant expressions. */

enum {
    TOK_BASE = 256,
    TOK_LAST = 256 - 1,
    TOK_NEXT = TOK_LAST + 3,
};

int main(void) {
    if (TOK_LAST != 255) return 1;
    if (TOK_NEXT != 258) return 2;
    return 42;
}

