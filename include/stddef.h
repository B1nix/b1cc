/* Freestanding stddef.h for b1cc */
#ifndef _STDDEF_H
#define _STDDEF_H

#ifdef __b1cc__

typedef long ptrdiff_t;
typedef unsigned long size_t;
typedef long ssize_t;

#define NULL ((void *)0)
#define offsetof(type, member) ((size_t)&((type *)0)->member)

#ifndef __cplusplus
typedef int wchar_t;
#endif

#else
#include_next <stddef.h>
#endif

#endif /* _STDDEF_H */
