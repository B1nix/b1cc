/* M34: mutually recursive object-like macros must terminate via the hideset
 * rather than expand forever; A->B->A stops with A left as an identifier. */
#define A B
#define B A
int A(void) { return 42; }   /* expands to: int A(void) ... (A hidden in B's body) */
int main(void) { return A(); }
