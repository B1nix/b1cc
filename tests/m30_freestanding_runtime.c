/* M30: Test freestanding runtime library functions */
#include <stddef.h>

/* Declare runtime functions */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
int snprintf(char *buf, size_t size, const char *fmt, ...);
long atol(const char *s);

int main(void) {
    int status = 0;

    /* memcpy */
    char src[] = "hello";
    char dst[16];
    memcpy(dst, src, 6);
    if (strcmp(dst, "hello") != 0) status = 1;

    /* memset */
    char buf[8];
    memset(buf, 0, 8);
    for (int i = 0; i < 8; ++i) {
        if (buf[i] != 0) status = 1;
    }
    memset(buf, 'A', 8);
    for (int i = 0; i < 8; ++i) {
        if (buf[i] != 'A') status = 1;
    }

    /* memcmp */
    char a[] = "abc";
    char b[] = "abd";
    if (memcmp(a, b, 3) >= 0) status = 1;
    if (memcmp(a, a, 3) != 0) status = 1;

    /* strlen */
    if (strlen("hello") != 5) status = 1;
    if (strlen("") != 0) status = 1;

    /* strcmp / strncmp */
    if (strcmp("abc", "abc") != 0) status = 1;
    if (strcmp("abc", "abd") >= 0) status = 1;
    if (strncmp("abcdef", "abcxyz", 3) != 0) status = 1;
    if (strncmp("abcdef", "abcxyz", 4) == 0) status = 1;

    /* strcpy */
    char sbuf[16];
    strcpy(sbuf, "test");
    if (strcmp(sbuf, "test") != 0) status = 1;

    /* strchr / strrchr */
    const char *hay = "hello world";
    if (strchr(hay, 'o') != hay + 4) status = 1;
    if (strrchr(hay, 'o') != hay + 7) status = 1;
    if (strchr(hay, 'z') != (void *)0) status = 1;

    /* memmove (overlapping) */
    char overlap[] = "abcdef";
    memmove(overlap + 2, overlap, 4);
    if (overlap[0] != 'a') status = 1;
    if (overlap[2] != 'a') status = 1;
    if (overlap[3] != 'b') status = 1;
    if (overlap[4] != 'c') status = 1;
    if (overlap[5] != 'd') status = 1;

    /* snprintf */
    char nbuf[64];
    int len = snprintf(nbuf, sizeof(nbuf), "val=%d", 42);
    if (strcmp(nbuf, "val=42") != 0) status = 1;
    if (len != 6) status = 1;

    /* atol */
    if (atol("123") != 123) status = 1;
    if (atol("-42") != -42) status = 1;
    if (atol("0") != 0) status = 1;

    (void)dst;

    return status;
}
