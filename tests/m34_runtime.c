/* M34: runtime / startup / target integration on the hosted profile. Verifies
 * main startup with argc/argv, environment access, exit status, atexit, file
 * I/O, and errno on a failed library call. Invoked as: m34_runtime one two
 * with M34VAR=ok in the environment; returns 42 on success. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int cleanup_ran = 0;
static void on_exit_cb(void) { cleanup_ran = 1; (void)cleanup_ran; }

int main(int argc, char **argv) {
    /* startup: argc/argv wired */
    if (argc != 3) return 1;
    if (strcmp(argv[1], "one") != 0 || strcmp(argv[2], "two") != 0) return 2;
    /* environment */
    char *e = getenv("M34VAR");
    if (e == 0 || strcmp(e, "ok") != 0) return 3;
    /* atexit registration */
    if (atexit(on_exit_cb) != 0) return 4;
    /* file I/O round-trip */
    FILE *f = fopen("/tmp/m34_runtime.tmp", "w");
    if (f == 0) return 5;
    fputs("hi", f);
    fclose(f);
    f = fopen("/tmp/m34_runtime.tmp", "r");
    if (f == 0) return 6;
    if (fgetc(f) != 'h') { fclose(f); return 7; }
    fclose(f);
    remove("/tmp/m34_runtime.tmp");
    /* errno set on a failed open */
    errno = 0;
    if (fopen("/no/such/dir/file", "r") != 0 || errno == 0) return 8;
    /* heap: malloc/realloc/free and calloc zero-initialization */
    int *a = malloc(4 * sizeof(int));
    if (a == 0) return 9;
    for (int i = 0; i < 4; i++) a[i] = i * 10;
    a = realloc(a, 8 * sizeof(int));
    if (a == 0) return 10;
    if (a[3] != 30) return 11;            /* realloc preserved contents */
    free(a);
    int *z = calloc(16, sizeof(int));
    if (z == 0) return 12;
    for (int i = 0; i < 16; i++) if (z[i] != 0) return 13;  /* calloc zeroed */
    free(z);
    return 42;
}
