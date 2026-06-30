/* tests/m18_float_aggregate_abi.c - M18 pure floating aggregate ABI coverage */

struct F2 {
    float x;
    float y;
};

struct D2 {
    double x;
    double y;
};

struct F2 make_f2(float x, float y) {
    struct F2 v = { x, y };
    return v;
}

float sum_f2(struct F2 v) {
    return v.x + v.y;
}

struct D2 make_d2(double x, double y) {
    struct D2 v = { x, y };
    return v;
}

double sum_d2(struct D2 v) {
    return v.x + v.y;
}

int main(void) {
    struct F2 f = make_f2(1.25f, 2.75f);
    if ((int)(f.x * 100.0f) != 125) return 1;
    if ((int)(f.y * 100.0f) != 275) return 2;
    if ((int)sum_f2(f) != 4) return 3;

    struct D2 d = make_d2(10.5, 31.5);
    if ((int)d.x != 10) return 4;
    if ((int)d.y != 31) return 5;
    if ((int)sum_d2(d) != 42) return 6;

    return 0;
}
