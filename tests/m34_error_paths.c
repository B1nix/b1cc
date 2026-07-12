/* M34: deterministic hosted allocation and I/O failures must report errors. */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

int open(const char *, int, ...);
int read(int, void *, size_t);
int write(int, const void *, size_t);
int close(int);
int fileno(FILE *);
int fflush(FILE *);

int main(void) {
    char byte = 0;

    errno = 0;
    if (fopen("/definitely/missing/b1cc", "r") != 0 || errno == 0) return 1;

    errno = 0;
    if (read(-1, &byte, 1) != -1 || errno == 0) return 2;
    errno = 0;
    if (write(-1, &byte, 1) != -1 || errno == 0) return 3;
    errno = 0;
    if (close(-1) != -1 || errno == 0) return 4;

    FILE *closed_fd_stream = fopen("/dev/null", "w");
    if (!closed_fd_stream) return 5;
    if (close(fileno(closed_fd_stream)) != 0) return 5;
    errno = 0;
    if (fputs("x", closed_fd_stream) == EOF || fflush(closed_fd_stream) != EOF || errno == 0) return 5;
    fclose(closed_fd_stream);

    errno = 0;
    if (malloc((size_t)-1) != 0) return 6;
    errno = 0;
    if (calloc((size_t)-1, 2) != 0) return 7;
    errno = 0;
    if (realloc((void *)0, (size_t)-1) != 0) return 8;
    return 0;
}
