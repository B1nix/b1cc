/* M34: a partial aggregate initializer must zero-initialize the omitted
 * members/elements of a *local* (automatic-storage) object, not leave stack
 * garbage. Returns 42 only if every omitted field/element reads back as zero. */

struct Big { long a; long b; long c; long d; long e; long f; long g; long h; };

static int check_struct(void) {
    struct Big s = {7};            /* only .a set; b..h must be zero */
    long *p = (long *)&s;
    int i = 1;
    while (i < 8) {
        if (p[i] != 0) return 0;
        i = i + 1;
    }
    return s.a == 7;
}

static int check_array(void) {
    int arr[6] = {1, 2};           /* [2..5] must be zero */
    int i = 2;
    while (i < 6) {
        if (arr[i] != 0) return 0;
        i = i + 1;
    }
    return arr[0] == 1 && arr[1] == 2;
}

/* Fill a stack region with nonzero values so a missed zero-fill in a later
 * aggregate placed at the same offsets would surface as garbage. */
static int dirty_stack(void) {
    int junk[16];
    int i = 0;
    while (i < 16) { junk[i] = 1431655765; i = i + 1; }
    return junk[7];
}

int main(void) {
    if (dirty_stack() == 0) return 1;
    if (!check_struct()) return 2;
    if (!check_array()) return 3;
    return 42;
}
