typedef struct AliasPair {
    int x;
    int y;
}
AliasPair;

typedef struct CTypeLike {
    int t;
    struct Missing *ref;
}
CTypeLike;

int main(void) {
    struct Pair {
        int x;
        int y;
    };
    char buf[4], *e;
    char left[2], right[3], *p;
    char *name, scratch[4];
    struct Pair first, second = {0};
    AliasPair alias_first, alias_second = {0};
    CTypeLike ctype_first, ctype_second = {0};
    buf[0] = 'o';
    buf[1] = 'k';
    buf[2] = 0;
    e = buf;
    if (e[0] != 'o') return 1;
    if (e[1] != 'k') return 2;
    left[0] = 'a';
    right[0] = 'b';
    right[1] = 'c';
    p = right;
    name = scratch;
    scratch[0] = 'z';
    scratch[1] = 0;
    if (left[0] != 'a') return 3;
    if (p[1] != 'c') return 4;
    if (name[0] != 'z') return 11;
    first.x = 10;
    first.y = 20;
    if (second.x != 0) return 5;
    if (first.y != 20) return 6;
    alias_first.x = 30;
    if (alias_second.x != 0) return 7;
    if (alias_first.x != 30) return 8;
    ctype_first.t = 11;
    if (ctype_second.t != 0) return 9;
    if (ctype_first.t != 11) return 10;
    return 42;
}
