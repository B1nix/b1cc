/* Freestanding stdarg.h for b1cc */
#ifndef _STDARG_H
#define _STDARG_H

#ifdef __b1cc__

typedef void *__builtin_va_list;
typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(dst, src)  __builtin_va_copy(dst, src)

#else
#include_next <stdarg.h>
#endif

#endif /* _STDARG_H */
