/* tests/c23_constexpr.c — Test C23 constexpr scalar variables */
constexpr int LIMIT = 50 + 50;

int main(void) {
    constexpr int multiplier = 2;
    constexpr int res = LIMIT * multiplier;

    if (res != 200) {
        return 1;
    }

    return 0;
}
