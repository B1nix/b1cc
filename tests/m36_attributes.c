/* M36: Comprehensive Extended GCC Attributes Test */

static int target_func(int x) {
    return x + 10;
}

int alias_func(int x) __attribute__((alias("target_func")));

static inline int __attribute__((always_inline)) inline_helper(int y) {
    return y * 2;
}

int __attribute__((visibility("default"))) public_func(void) {
    return 42;
}

int main(void) {
    if (alias_func(32) != 42) return 1;
    if (inline_helper(21) != 42) return 2;
    if (public_func() != 42) return 3;
    return 0;
}
