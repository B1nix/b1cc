/* Minimal freestanding runtime library for b1cc.
 * Provides basic libc functions needed by b1cc-compiled programs
 * without depending on the host or target system libc. */

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; ++i) d[i] = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) ++len;
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) ++d;
    while ((*d++ = *src++));
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (*d) ++d;
    for (size_t i = 0; i < n && src[i]; ++i) *d++ = src[i];
    *d = '\0';
    return dst;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        ++s;
    }
    return (c == '\0') ? (char *)s : (void *)0;
}

char *strrchr(const char *s, int c) {
    const char *last = (void *)0;
    while (*s) {
        if (*s == (char)c) last = s;
        ++s;
    }
    return (c == '\0') ? (char *)s : (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; ++haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { ++h; ++n; }
        if (!*n) return (char *)haystack;
    }
    return (void *)0;
}

/* Integer to string conversion (signed, base 10) */
static void write_num(char *buf, unsigned long val, int base, int *pos) {
    if (val == 0) {
        buf[(*pos)++] = '0';
        return;
    }
    char tmp[24];
    int len = 0;
    while (val) {
        int digit = val % base;
        tmp[len++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        val /= base;
    }
    for (int i = len - 1; i >= 0; --i) {
        buf[(*pos)++] = tmp[i];
    }
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    /* Minimal implementation: handles %d, %u, %x, %s, %ld, %lu, %lx, %%, and %zu */
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    size_t pos = 0;
    const char *f = fmt;
    while (*f && pos < size - 1) {
        if (*f != '%') {
            buf[pos++] = *f++;
            continue;
        }
        ++f;
        if (*f == '%') {
            buf[pos++] = '%';
            ++f;
            continue;
        }
        /* skip flags/width */
        while (*f == '-' || *f == '+' || *f == ' ' || *f == '0' || *f == '#') ++f;
        while (*f >= '0' && *f <= '9') ++f;
        if (*f == 'l') { ++f; }
        if (*f == 'z') { ++f; }
        switch (*f) {
        case 'd': case 'i': {
            long val = __builtin_va_arg(ap, long);
            if (val < 0 && pos < size - 1) { buf[pos++] = '-'; val = -val; }
            write_num(buf, (unsigned long)val, 10, (int *)&pos);
            break;
        }
        case 'u': {
            unsigned long val = __builtin_va_arg(ap, unsigned long);
            write_num(buf, val, 10, (int *)&pos);
            break;
        }
        case 'x': {
            unsigned long val = __builtin_va_arg(ap, unsigned long);
            write_num(buf, val, 16, (int *)&pos);
            break;
        }
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && pos < size - 1) buf[pos++] = *s++;
            break;
        }
        case 'p': {
            unsigned long val = (unsigned long)__builtin_va_arg(ap, void *);
            buf[pos++] = '0';
            buf[pos++] = 'x';
            write_num(buf, val, 16, (int *)&pos);
            break;
        }
        case 'c': {
            int c = __builtin_va_arg(ap, int);
            if (pos < size - 1) buf[pos++] = (char)c;
            break;
        }
        default:
            if (pos < size - 1) buf[pos++] = '%';
            if (pos < size - 1) buf[pos++] = *f;
            break;
        }
        ++f;
    }
    __builtin_va_end(ap);
    if (size > 0) buf[pos < size ? pos : size - 1] = '\0';
    return (int)pos;
}

int sprintf(char *buf, const char *fmt, ...) {
    /* Very unsafe: no bounds checking. Use snprintf in new code. */
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int pos = 0;
    const char *f = fmt;
    while (*f) {
        if (*f != '%') { buf[pos++] = *f++; continue; }
        ++f;
        if (*f == '%') { buf[pos++] = '%'; ++f; continue; }
        while (*f == '-' || *f == '+' || *f == ' ' || *f == '0' || *f == '#') ++f;
        while (*f >= '0' && *f <= '9') ++f;
        if (*f == 'l') { ++f; }
        if (*f == 'z') { ++f; }
        switch (*f) {
        case 'd': case 'i': {
            long val = __builtin_va_arg(ap, long);
            if (val < 0) { buf[pos++] = '-'; val = -val; }
            write_num(buf, (unsigned long)val, 10, &pos);
            break;
        }
        case 'u': {
            unsigned long val = __builtin_va_arg(ap, unsigned long);
            write_num(buf, val, 10, &pos);
            break;
        }
        case 'x': {
            unsigned long val = __builtin_va_arg(ap, unsigned long);
            write_num(buf, val, 16, &pos);
            break;
        }
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) buf[pos++] = *s++;
            break;
        }
        case 'p': {
            unsigned long val = (unsigned long)__builtin_va_arg(ap, void *);
            buf[pos++] = '0';
            buf[pos++] = 'x';
            write_num(buf, val, 16, &pos);
            break;
        }
        case 'c': {
            int c = __builtin_va_arg(ap, int);
            buf[pos++] = (char)c;
            break;
        }
        default:
            buf[pos++] = '%';
            buf[pos++] = *f;
            break;
        }
        ++f;
    }
    __builtin_va_end(ap);
    buf[pos] = '\0';
    return pos;
}

/* atol: convert string to long */
long atol(const char *s) {
    long result = 0;
    int neg = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') ++s;
    if (*s == '-') { neg = 1; ++s; }
    else if (*s == '+') { ++s; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        ++s;
    }
    return neg ? -result : result;
}

int atoi(const char *s) {
    return (int)atol(s);
}

/* abort: loop forever */
void abort(void) {
    volatile int x = 1;
    for (;;) x = !x;
}

/* exit: loop forever */
void exit(int code) {
    volatile int x = code;
    for (;;) x = !x;
}
