/* tests/m21_dedicated_expander.c — exercises the dedicated preprocessing-token
 * expander (Prosser-style, per-token hidesets, argument prescan). Each case
 * below is one the old lex-reuse + fixpoint path handled fragilely or wrongly.
 * Self-checking: returns 0 on success. */

/* 1. Object-like macro that expands to a function-like macro name, whose
 *    argument list lives in the *following* stream (X-macro alias pattern). */
#define TARGET(name, value) int fn_##name(void) { return value; }
#define ALIAS TARGET
ALIAS(alpha, 7)

/* 2. Argument prescan: a parameter not adjacent to ## is fully expanded before
 *    substitution, so PASTE-through-a-wrapper sees the expanded argument. */
#define PASTE(a, b) a ## b
#define WRAP(a, b) PASTE(a, b)
#define TWO 2
int V1TWO = 11;   /* PASTE keeps TWO literal   -> V1 ## TWO  -> V1TWO */
int V12 = 22;     /* WRAP expands TWO first     -> V1 ## 2    -> V12  */

/* 3. Mutual recursion terminates via hidesets (blue paint), not an iteration
 *    cap: p->q->p, and p is painted, so it settles as the identifier p. */
#define p q
#define q p
int p = 33;       /* the declared object is named p after expansion settles */

/* 4. Stringize uses the raw argument. */
#define STR(x) #x
const char *s = STR(a   b);   /* -> "a b" (single-space normalized) */

int main(void) {
    if (fn_alpha() != 7) return 1;
    if (PASTE(V1, TWO) != 11) return 2;
    if (WRAP(V1, TWO) != 22) return 3;
    if (p != 33) return 4;
    if (s[0] != 'a') return 5;
    return 0;
}
