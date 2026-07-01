/* tests/m21_repeat_xmacro.c - X-macro headers can be included repeatedly. */

#define M21_REPEAT(name) int first_##name(void) { return 10; }
#include <m21_repeat_xmacro.h>
#undef M21_REPEAT

#define M21_REPEAT(name) int second_##name(void) { return 11; }
#include <m21_repeat_xmacro.h>
#undef M21_REPEAT

int main(void) {
    return first_one() + first_two() + second_one() + second_two();
}

