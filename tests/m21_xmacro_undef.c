/* tests/m21_xmacro_undef.c - Macro expansion must happen before header-local #undef. */
#include <m21_xmacro_undef.h>

#define M21_ALIAS_TARGET(name, value) int alias_##name(void) { return value; }
#define M21_ALIAS M21_ALIAS_TARGET
M21_ALIAS(beta, 9)
#undef M21_ALIAS
#undef M21_ALIAS_TARGET

#ifdef M21_ITEM
int macro_was_not_undefined(void) { return 99; }
#endif

int main(void) {
    return generated_alpha() + alias_beta() + 12;
}
