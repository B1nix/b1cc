/* M34: comprehensive preprocessor edge cases — hex/char constants in #if,
 * nested conditionals, complex ## paste, rescanning, and placemarker paste. */

#define HEX_VAL 0xFF
#define CHAR_A 'A'
#define CHAR_Z 'Z'
#define CHAR_0 '0'

/* Hex constant in #if */
#if HEX_VAL != 255
#error hex constant mismatch
#endif

/* Character constant in #if */
#if CHAR_A != 65
#error char constant A mismatch
#endif

#if CHAR_Z != 90
#error char constant Z mismatch
#endif

/* Hex literal in #if directly */
#if 0xDEAD != 57005
#error inline hex mismatch
#endif

/* Mixed hex and decimal */
#if (0x10 + 5) != 21
#error mixed hex+dec mismatch
#endif

/* Nested #if with hex */
#if 1
  #if 0xFF > 200
    #define NESTED_HEX 1
  #else
    #error nested hex condition wrong
  #endif
#else
  #error nested else wrong
#endif

#ifndef NESTED_HEX
#error NESTED_HEX not defined
#endif

/* Complex ## paste edge cases */
#define PASTE3(a, b, c) a ## b ## c
#define STR(x) #x
#define XSTR(x) STR(x)

/* Triple paste */
#define VAL3 PASTE3(1, 2, 3)

/* Stringize of paste result */
#define CAT2(a, b) a ## b
#define XCAT(a, b) CAT2(a, b)

/* Placemarker paste (empty arg) */
#define GLUE_EMPTY(a, b) a ## b

/* Rescanning: macro expands to another macro invocation */
#define CALL(x) x
#define FUNC(a) (a + 10)

/* X-macro alias pattern */
#define ALIAS_TARGET 42

/* Indirect expansion with hideset */
#define FIRST SECOND
#define SECOND 99

int main(void) {
    /* Hex constant value */
    int hv = HEX_VAL;
    if (hv != 255) return 1;

    /* Char constant values */
    int ca = CHAR_A;
    if (ca != 65) return 2;
    int cz = CHAR_Z;
    if (cz != 90) return 3;

    /* Inline hex */
    int hx = 0xAB;
    if (hx != 171) return 4;

    /* Triple paste */
    int v3 = VAL3;
    if (v3 != 123) return 5;

    /* Stringize of hex */
    int hex_str_val = 0x10;
    (void)hex_str_val;

    /* Placemarker paste: GLUE_EMPTY(42, ) -> 42 ## <empty> -> 42 */
    int glue_result = GLUE_EMPTY(42, );
    if (glue_result != 42) return 6;

    /* Rescanning: CALL(FUNC(5)) should expand both */
    int rs = CALL(FUNC(5));
    if (rs != 15) return 7;

    /* Indirect expansion hideset: FIRST expands to SECOND, SECOND expands to 99 */
    int ind = FIRST;
    if (ind != 99) return 8;

    return 42;
}
