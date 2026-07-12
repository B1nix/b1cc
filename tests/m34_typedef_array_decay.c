/* M34: a variable of a typedef'd array type is itself an array and must decay to
 * a pointer when passed to a function (regression: it loaded element 0's value
 * instead of the address, which broke setjmp's jmp_buf — a NULL was passed to
 * setjmp). Both file-scope and local objects must decay. */
typedef long buf_t[8];
static buf_t g;

static int fill(long *p) { p[0] = 42; return (int)p[0]; }

int main(void) {
    if (sizeof(buf_t) != 64) return 1;   /* typedef array size preserved */
    /* file-scope typedef-array decays to &g and the write lands in g */
    if (fill(g) != 42) return 2;
    if (g[0] != 42) return 3;
    /* local typedef-array decays to its address too */
    buf_t local;
    if (fill(local) != 42) return 4;
    local[0] = 7;
    if (local[0] != 7) return 5;
    return 42;
}
