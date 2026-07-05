/* Freestanding stdbool.h for b1cc */
#ifndef _STDBOOL_H
#define _STDBOOL_H

#ifdef __b1cc__

#define bool _Bool
#define true 1
#define false 0
#define __bool_true_false_are_defined 1

#else
#include_next <stdbool.h>
#endif

#endif /* _STDBOOL_H */
