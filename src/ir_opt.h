#ifndef IR_OPT_H
#define IR_OPT_H

#include "ir.h"

void ir_optimize_function(IrFunction *fn, Arena *arena);
void ir_optimize_module(IrFunctionArray *functions, Arena *arena);

#endif /* IR_OPT_H */
