/* M34: wide string literals (L"...") are wchar_t (4-byte element) arrays, not
 * byte strings. Verifies element values, null termination, escapes, wcslen,
 * and adjacent-literal concatenation. */
#include <wchar.h>
#include <stddef.h>

/* wide string literal in a file-scope initializer (distinct code path from a
 * local initializer). */
static const int *g_wide = (const int *)L"Go";   /* {71, 111, 0} */

int main(void) {
    if (g_wide[0] != 71 || g_wide[1] != 111 || g_wide[2] != 0) return 7;

    const wchar_t *s = L"hi";
    if (s[0] != 104 || s[1] != 105 || s[2] != 0) return 1;
    if ((int)wcslen(s) != 2) return 2;

    /* escapes decode to their control values */
    const wchar_t *e = L"a\tb\n";
    if (e[0] != 'a' || e[1] != 9 || e[2] != 'b' || e[3] != 10 || e[4] != 0) return 3;
    if ((int)wcslen(e) != 4) return 4;

    /* adjacent wide string literals concatenate */
    const wchar_t *c = L"ab" L"cd";
    if ((int)wcslen(c) != 4 || c[3] != 'd') return 5;

    /* a wide element is 4 bytes wide (distinct from a narrow byte string) */
    const wchar_t *w = L"AB";
    if (w[0] != 65 || w[1] != 66) return 6;

    return 42;
}
