#include "ir_opt.h"
#include <stdlib.h>
#include <string.h>

void ir_optimize_function(IrFunction *fn, Arena *arena) {
    (void)arena;
    if (!fn || fn->code.count == 0) return;

    /* Pass 1: Dead Code Elimination (DCE) after unconditional return or jump */
    IrInstArray new_code;
    ir_inst_array_init(&new_code);
    
    bool unreachable = false;
    for (int i = 0; i < fn->code.count; ++i) {
        IrInst *inst = &fn->code.data[i];
        
        if (inst->op == IR_LABEL) {
            unreachable = false;
        }
        
        if (!unreachable) {
            ir_inst_array_push(&new_code, inst);
            if (inst->op == IR_RET || inst->op == IR_RET64 || inst->op == IR_JMP || inst->op == IR_TRAP) {
                unreachable = true;
            }
        }
    }
    
    fn->code = new_code;
}

void ir_optimize_module(IrFunctionArray *functions, Arena *arena) {
    if (!functions) return;
    for (int i = 0; i < functions->count; ++i) {
        ir_optimize_function(&functions->data[i], arena);
    }
}
