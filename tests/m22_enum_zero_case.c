/* tests/m22_enum_zero_case.c - First enum constant with value 0 is usable in case labels. */

enum {
    TOK_FIRST,
    TOK_SECOND,
};

int main(void) {
    enum {
        LOCAL_FIRST,
        LOCAL_SECOND,
    };
    int tok = LOCAL_FIRST;
    switch (tok) {
    case LOCAL_FIRST:
        break;
    case LOCAL_SECOND:
        return 1;
    }
    tok = TOK_FIRST;
    switch (tok) {
    case TOK_FIRST:
        return 42;
    case TOK_SECOND:
        return 1;
    }
    return 2;
}
