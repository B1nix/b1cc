/* M36: Comprehensive DWARF Debug Info Generation Test */

struct Point {
    int x;
    int y;
};

static struct Point global_pt = { 10, 32 };

static int compute_point_sum(struct Point *p) {
    int sum = p->x + p->y;
    return sum;
}

int main(void) {
    struct Point local_pt = { 20, 22 };
    if (global_pt.x + global_pt.y != 42) return 1;
    if (compute_point_sum(&local_pt) != 42) return 2;
    return 0;
}
