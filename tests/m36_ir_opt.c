/* M36: Comprehensive IR Optimization Test */

static int compute_folded_constants(void) {
    int a = 10 + 20 - 5; // 25
    int b = a * 2;       // 50
    int c = b / 5;       // 10
    return c + 32;       // 42
}

static int dead_code_elimination_helper(int input) {
    if (input > 0) {
        return input + 10;
        /* Unreachable instructions */
        input = 999;
        return input * 100;
    } else {
        return 42;
        /* Unreachable instructions */
        return 0;
    }
    return -1;
}

int main(void) {
    if (compute_folded_constants() != 42) return 1;
    if (dead_code_elimination_helper(32) != 42) return 2;
    if (dead_code_elimination_helper(-5) != 42) return 3;
    return 0;
}
