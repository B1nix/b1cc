/* M34: nested anonymous struct/union partial init — omitted fields must be
 * zero-filled, not left as stack garbage. */

struct Inner {
    long x;
    long y;
    long z;
};

struct Outer {
    long before;
    struct Inner anon_struct;  /* anonymous nested struct */
    long after;
};

/* Nested anonymous union partial init */
struct WithUnion {
    long tag;
    union {
        long a;
        long b;
        long c;
    } anon_union;
    long tail;
};

/* Multi-level nesting: struct containing struct containing struct */
struct Level1 {
    long a;
    struct {
        long b;
        struct {
            long c;
            long d;
        } inner;
        long e;
    } mid;
    long f;
};

static int check_nested_struct(void) {
    struct Outer o = { .before = 1, .after = 9 };
    /* .anon_struct has .x, .y, .z all omitted — must be zero */
    if (o.before != 1) return 1;
    if (o.after != 9) return 2;
    if (o.anon_struct.x != 0) return 3;
    if (o.anon_struct.y != 0) return 4;
    if (o.anon_struct.z != 0) return 5;
    return 0;
}

static int check_partial_anon_struct(void) {
    struct Outer o = { .before = 10, .anon_struct = { .y = 77 }, .after = 20 };
    if (o.before != 10) return 1;
    if (o.anon_struct.x != 0) return 2;
    if (o.anon_struct.y != 77) return 3;
    if (o.anon_struct.z != 0) return 4;
    if (o.after != 20) return 5;
    return 0;
}

static int check_union_first_member(void) {
    /* Union initializes first member */
    struct WithUnion wu = { .tag = 1, .anon_union = { .a = 42 } };
    if (wu.tag != 1) return 1;
    if (wu.anon_union.a != 42) return 2;
    if (wu.anon_union.b != 42) return 3;  /* same storage as a */
    if (wu.tail != 0) return 4;  /* tail omitted, must be zero */
    return 0;
}

static int check_partial_union(void) {
    struct WithUnion wu = { .tag = 5, .tail = 99 };
    /* .anon_union omitted entirely — first member 'a' must be zero */
    if (wu.tag != 5) return 1;
    if (wu.anon_union.a != 0) return 2;
    if (wu.tail != 99) return 3;
    return 0;
}

static int check_deep_nesting(void) {
    struct Level1 l = { .a = 1, .mid = { .b = 2, .inner = { .c = 3 }, .e = 5 }, .f = 6 };
    /* .mid.inner.d is omitted — must be zero */
    if (l.a != 1) return 1;
    if (l.mid.b != 2) return 2;
    if (l.mid.inner.c != 3) return 3;
    if (l.mid.inner.d != 0) return 4;
    if (l.mid.e != 5) return 5;
    if (l.f != 6) return 6;
    return 0;
}

static int check_deep_nesting_full_zero(void) {
    struct Level1 l = { .a = 10 };
    /* Only .a set; everything else must be zero */
    if (l.a != 10) return 1;
    if (l.mid.b != 0) return 2;
    if (l.mid.inner.c != 0) return 3;
    if (l.mid.inner.d != 0) return 4;
    if (l.mid.e != 0) return 5;
    if (l.f != 0) return 6;
    return 0;
}

/* Dirty stack to surface missed zero-fills */
static void dirty_stack(void) {
    long junk[32];
    int i = 0;
    while (i < 32) { junk[i] = 0x5555555555555555L; i = i + 1; }
}

int main(void) {
    dirty_stack();
    if (check_nested_struct()) return 10 + check_nested_struct();
    dirty_stack();
    if (check_partial_anon_struct()) return 20 + check_partial_anon_struct();
    dirty_stack();
    if (check_union_first_member()) return 30 + check_union_first_member();
    dirty_stack();
    if (check_partial_union()) return 40 + check_partial_union();
    dirty_stack();
    if (check_deep_nesting()) return 50 + check_deep_nesting();
    dirty_stack();
    if (check_deep_nesting_full_zero()) return 60 + check_deep_nesting_full_zero();
    return 42;
}
