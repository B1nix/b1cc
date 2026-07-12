/* A2 aggregate assignment: array field, object larger than 16 bytes, and
 * local/global/pointer copies. */

struct Wide {
    char bytes[13];
    long tail;
};

struct Wide global_src;
struct Wide global_dst;

static void fill(struct Wide *p, char base, long tail) {
    p->bytes[0] = base;
    p->bytes[1] = base + 1;
    p->bytes[12] = base + 12;
    p->tail = tail;
}

int main(void) {
    struct Wide local_src;
    struct Wide local_dst;
    struct Wide *p;

    fill(&local_src, 10, 1000);
    local_dst = local_src;
    local_src.bytes[0] = 90;
    local_src.bytes[12] = 92;
    local_src.tail = 9000;
    if (local_dst.bytes[0] != 10) return 1;
    if (local_dst.bytes[1] != 11) return 2;
    if (local_dst.bytes[12] != 22) return 3;
    if (local_dst.tail != 1000) return 4;

    fill(&global_src, 20, 2000);
    global_dst = global_src;
    global_src.bytes[0] = 80;
    global_src.bytes[12] = 82;
    global_src.tail = 8000;
    if (global_dst.bytes[0] != 20) return 5;
    if (global_dst.bytes[1] != 21) return 6;
    if (global_dst.bytes[12] != 32) return 7;
    if (global_dst.tail != 2000) return 8;

    p = &global_dst;
    *p = local_src;
    if (global_dst.bytes[0] != 90) return 9;
    if (global_dst.bytes[12] != 92) return 10;
    if (global_dst.tail != 9000) return 11;

    return 0;
}
