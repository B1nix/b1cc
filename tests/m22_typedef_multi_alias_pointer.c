/* tests/m22_typedef_multi_alias_pointer.c - Typedefs may declare value and pointer aliases together. */

typedef struct Header {
    int magic;
} Header, *PHeader;

int main(void) {
    Header h;
    PHeader p;
    h.magic = 42;
    p = &h;
    return p->magic;
}
