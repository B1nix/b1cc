#include "backend_target.h"
#include "diagnostics.h"
#include <sstream>

namespace Backend {
  using namespace IR;

  class Arm64Target : public TargetBackend {
  private:
    static bool is_defined_global(const std::string &name) {
      for (const auto &g : global_decls) {
        if (g.name == name)
          return true;
      }
      return false;
    }

  public:
    std::string emit_globals(const std::vector<IrGlobalVar> &globals) override {
      std::ostringstream out;
      if (globals.empty())
        return "";
      out << ".data\n";
      for (const auto &g : globals) {
        if (!g.is_static) {
          out << ".globl _" << g.name << "\n";
        }
        int align = (g.elem_size == 8) ? 3 : (g.elem_size == 4) ? 2 : (g.elem_size == 2) ? 1 : 0;
        out << ".p2align " << align << "\n";
        out << "_" << g.name << ":\n";
        if (g.is_array) {
          for (long val : g.initializers) {
            if (g.elem_size == 1)
              out << "    .byte " << (val & 0xff) << "\n";
            else if (g.elem_size == 2)
              out << "    .short " << (val & 0xffff) << "\n";
            else if (g.elem_size == 4)
              out << "    .long " << val << "\n";
            else
              out << "    .quad " << val << "\n";
          }
          long remaining = g.size - g.initializers.size();
          if (remaining > 0) {
            out << "    .zero " << (remaining * g.elem_size) << "\n";
          }
        } else {
          long val = g.initializers.empty() ? 0 : g.initializers[0];
          if (g.elem_size == 1)
            out << "    .byte " << (val & 0xff) << "\n";
          else if (g.elem_size == 2)
            out << "    .short " << (val & 0xffff) << "\n";
          else if (g.elem_size == 4)
            out << "    .long " << val << "\n";
          else
            out << "    .quad " << val << "\n";
        }
      }
      out << "\n";
      return out.str();
    }

    std::string emit_function(const IrFunction &fn) override {
      std::ostringstream out;
      if (!fn.strings.empty()) {
        out << ".cstring\n";
        for (const auto &s : fn.strings)
          out << s.first << ":\n    .asciz " << s.second << "\n";
      }
      out << ".text\n";
      if (!fn.is_static) {
        out << ".globl _" << fn.name << "\n";
      }
      out << ".p2align 2\n_" << fn.name << ":\n";
      const bool frame = fn.has_call || !fn.locals.empty();
      if (frame) {
        out << "    stp x29, x30, [sp, #-16]!\n";
        out << "    mov x29, sp\n";
        if (!fn.locals.empty())
          out << "    sub sp, sp, #" << (fn.locals.size() * 16) << "\n";
        for (size_t i = 0; i < fn.params.size(); ++i) {
          if (i < 8) {
            out << "    str x" << i << ", [x29, #-" << ((i + 1) * 16) << "]\n";
          } else {
            out << "    ldr x8, [x29, #" << (16 + (i - 8) * 8) << "]\n";
            out << "    str x8, [x29, #-" << ((i + 1) * 16) << "]\n";
          }
        }
      }
      for (const IrInst &inst : fn.code) {
        if (inst.op == "const") {
          if (inst.value >= 0 && inst.value <= 65535) {
            out << "    mov x0, #" << inst.value << "\n";
          } else {
            unsigned long long val = inst.value;
            out << "    movz x0, #" << (val & 0xffff) << "\n";
            if (val & 0xffff0000ULL)
              out << "    movk x0, #" << ((val >> 16) & 0xffff) << ", lsl #16\n";
            if (val & 0xffff00000000ULL)
              out << "    movk x0, #" << ((val >> 32) & 0xffff) << ", lsl #32\n";
            if (val & 0xffff000000000000ULL)
              out << "    movk x0, #" << ((val >> 48) & 0xffff) << ", lsl #48\n";
          }
          out << "    str x0, [sp, #-16]!\n";
        } else if (inst.op == "load") {
          out << "    ldr x0, [x29, #-" << ((inst.value + 1) * 16) << "]\n";
          out << "    str x0, [sp, #-16]!\n";
        } else if (inst.op == "str") {
          out << "    adrp x0, " << inst.arg << "@PAGE\n";
          out << "    add x0, x0, " << inst.arg << "@PAGEOFF\n";
          out << "    str x0, [sp, #-16]!\n";
        } else if (inst.op == "store") {
          out << "    ldr x0, [sp], #16\n";
          out << "    str x0, [x29, #-" << ((inst.value + 1) * 16) << "]\n";
        } else if (inst.op == "pop") {
          out << "    add sp, sp, #16\n";
        } else if (inst.op == "+" || inst.op == "-" || inst.op == "*" ||
                   inst.op == "/" || inst.op == "%" || inst.op == "==" || inst.op == "!=" ||
                   inst.op == "<" || inst.op == ">" || inst.op == "<=" ||
                   inst.op == ">=" || inst.op == "index" ||
                   inst.op == "&" || inst.op == "|" || inst.op == "^" ||
                   inst.op == "<<" || inst.op == ">>") {
          out << "    ldr x0, [sp], #16\n";
          out << "    ldr x1, [sp], #16\n";
          if (inst.op == "index") {
            if (inst.value == 1) {
              out << "    ldrsb x0, [x1, x0]\n";
            } else if (inst.value == 2) {
              out << "    ldrsh x0, [x1, x0, lsl #1]\n";
            } else if (inst.value == 4) {
              out << "    ldrsw x0, [x1, x0, lsl #2]\n";
            } else {
              out << "    ldr x0, [x1, x0, lsl #3]\n";
            }
          } else if (inst.op == "+")
            out << "    add x0, x1, x0\n";
          else if (inst.op == "-")
            out << "    sub x0, x1, x0\n";
          else if (inst.op == "*")
            out << "    mul x0, x1, x0\n";
          else if (inst.op == "/")
            out << "    sdiv x0, x1, x0\n";
          else if (inst.op == "%") {
            out << "    sdiv x2, x1, x0\n";
            out << "    msub x0, x2, x0, x1\n";
          }
          else if (inst.op == "&")
            out << "    and x0, x1, x0\n";
          else if (inst.op == "|")
            out << "    orr x0, x1, x0\n";
          else if (inst.op == "^")
            out << "    eor x0, x1, x0\n";
          else if (inst.op == "<<")
            out << "    lsl x0, x1, x0\n";
          else if (inst.op == ">>")
            out << "    asr x0, x1, x0\n";
          else {
            out << "    cmp x1, x0\n";
            if (inst.op == "==")
              out << "    cset x0, eq\n";
            else if (inst.op == "!=")
              out << "    cset x0, ne\n";
            else if (inst.op == "<")
              out << "    cset x0, lt\n";
            else if (inst.op == ">")
              out << "    cset x0, gt\n";
            else if (inst.op == "<=")
              out << "    cset x0, le\n";
            else
              out << "    cset x0, ge\n";
          }
          out << "    str x0, [sp, #-16]!\n";
        } else if (inst.op == "~" || inst.op == "!" || inst.op == "neg" || inst.op == "cast") {
          out << "    ldr x0, [sp], #16\n";
          if (inst.op == "~")
            out << "    mvn x0, x0\n";
          else if (inst.op == "neg")
            out << "    neg x0, x0\n";
          else if (inst.op == "cast") {
            if (inst.value == 1)
              out << "    sxtb x0, w0\n";
            else if (inst.value == 2)
              out << "    sxth x0, w0\n";
            else if (inst.value == 4)
              out << "    sxtw x0, w0\n";
          } else {
            out << "    cmp x0, #0\n";
            out << "    cset x0, eq\n";
          }
          out << "    str x0, [sp, #-16]!\n";
        } else if (inst.op == "jz") {
          out << "    ldr x0, [sp], #16\n";
          out << "    cmp x0, #0\n";
          out << "    b.eq " << inst.arg << "\n";
        } else if (inst.op == "jmp") {
          out << "    b " << inst.arg << "\n";
        } else if (inst.op == "label") {
          out << inst.arg << ":\n";
        } else if (inst.op == "call" || inst.op == "icall") {
          long num_args = inst.value;
          if (num_args <= 8) {
            for (long i = num_args - 1; i >= 0; --i)
              out << "    ldr x" << i << ", [sp], #16\n";
            if (inst.op == "icall") {
              out << "    ldr x16, [sp], #16\n";
              out << "    blr x16\n";
            } else {
              out << "    bl _" << inst.arg << "\n";
            }
          } else {
            long num_stack_args = num_args - 8;
            for (long i = num_args - 1; i >= 8; --i) {
              int reg_idx = 8 + (int)(i - 8);
              out << "    ldr x" << reg_idx << ", [sp], #16\n";
            }
            for (long i = 7; i >= 0; --i) {
              out << "    ldr x" << i << ", [sp], #16\n";
            }
            if (inst.op == "icall") {
              out << "    ldr x16, [sp], #16\n";
            }
            long stack_bytes = ((num_stack_args + 1) / 2) * 16;
            out << "    sub sp, sp, #" << stack_bytes << "\n";
            for (long i = 8; i < num_args; ++i) {
              int reg_idx = 8 + (int)(i - 8);
              out << "    str x" << reg_idx << ", [sp, #" << ((i - 8) * 8) << "]\n";
            }
            if (inst.op == "icall") {
              out << "    blr x16\n";
            } else {
              out << "    bl _" << inst.arg << "\n";
            }
            out << "    add sp, sp, #" << stack_bytes << "\n";
          }
          out << "    str x0, [sp, #-16]!\n";
        } else if (inst.op == "addr") {
          out << "    sub x0, x29, #" << ((inst.value + 1) * 16) << "\n";
          out << "    str x0, [sp, #-16]!\n";
        } else if (inst.op == "gload") {
          int gsize = 8;
          if (global_var_elem_scales.count(inst.arg)) {
            gsize = global_var_elem_scales[inst.arg];
          }
          if (is_defined_global(inst.arg)) {
            out << "    adrp x0, _" << inst.arg << "@PAGE\n";
            if (gsize == 1) {
              out << "    ldrsb x0, [x0, _" << inst.arg << "@PAGEOFF]\n";
            } else if (gsize == 2) {
              out << "    ldrsh x0, [x0, _" << inst.arg << "@PAGEOFF]\n";
            } else if (gsize == 4) {
              out << "    ldrsw x0, [x0, _" << inst.arg << "@PAGEOFF]\n";
            } else {
              out << "    ldr x0, [x0, _" << inst.arg << "@PAGEOFF]\n";
            }
          } else {
            out << "    adrp x0, _" << inst.arg << "@GOTPAGE\n";
            out << "    ldr x0, [x0, _" << inst.arg << "@GOTPAGEOFF]\n";
            if (gsize == 1) {
              out << "    ldrsb x0, [x0]\n";
            } else if (gsize == 2) {
              out << "    ldrsh x0, [x0]\n";
            } else if (gsize == 4) {
              out << "    ldrsw x0, [x0]\n";
            } else {
              out << "    ldr x0, [x0]\n";
            }
          }
          out << "    str x0, [sp, #-16]!\n";
        } else if (inst.op == "gstore") {
          out << "    ldr x0, [sp], #16\n";
          int gsize = 8;
          if (global_var_elem_scales.count(inst.arg)) {
            gsize = global_var_elem_scales[inst.arg];
          }
          if (is_defined_global(inst.arg)) {
            out << "    adrp x1, _" << inst.arg << "@PAGE\n";
            if (gsize == 1) {
              out << "    strb w0, [x1, _" << inst.arg << "@PAGEOFF]\n";
            } else if (gsize == 2) {
              out << "    strh w0, [x1, _" << inst.arg << "@PAGEOFF]\n";
            } else if (gsize == 2) {
              out << "    strh w0, [x1, _" << inst.arg << "@PAGEOFF]\n";
            } else if (gsize == 4) {
              out << "    str w0, [x1, _" << inst.arg << "@PAGEOFF]\n";
            } else {
              out << "    str x0, [x1, _" << inst.arg << "@PAGEOFF]\n";
            }
          } else {
            out << "    adrp x1, _" << inst.arg << "@GOTPAGE\n";
            out << "    ldr x1, [x1, _" << inst.arg << "@GOTPAGEOFF]\n";
            if (gsize == 1) {
              out << "    strb w0, [x1]\n";
            } else if (gsize == 2) {
              out << "    strh w0, [x1]\n";
            } else if (gsize == 4) {
              out << "    str w0, [x1]\n";
            } else {
              out << "    str x0, [x1]\n";
            }
          }
        } else if (inst.op == "gaddr") {
          if (is_defined_global(inst.arg)) {
            out << "    adrp x0, _" << inst.arg << "@PAGE\n";
            out << "    add x0, x0, _" << inst.arg << "@PAGEOFF\n";
          } else {
            out << "    adrp x0, _" << inst.arg << "@GOTPAGE\n";
            out << "    ldr x0, [x0, _" << inst.arg << "@GOTPAGEOFF]\n";
          }
          out << "    str x0, [sp, #-16]!\n";
        } else if (inst.op == "store_index") {
          out << "    ldr x1, [sp], #16\n";
          out << "    ldr x2, [sp], #16\n";
          out << "    ldr x0, [sp], #16\n";
          if (inst.value == 1) {
            out << "    strb w0, [x1, x2]\n";
          } else if (inst.value == 2) {
            out << "    strh w0, [x1, x2, lsl #1]\n";
          } else if (inst.value == 4) {
            out << "    str w0, [x1, x2, lsl #2]\n";
          } else {
            out << "    str x0, [x1, x2, lsl #3]\n";
          }
        } else if (inst.op == "ret") {
          out << "    ldr x0, [sp], #16\n";
          if (frame) {
            out << "    mov sp, x29\n";
            out << "    ldp x29, x30, [sp], #16\n";
          }
          out << "    ret\n";
        } else {
          Diagnostics::fatal("unknown IR op " + inst.op);
        }
      }
      return out.str();
    }
  };

  TargetBackend* create_arm64_backend() {
    return new Arm64Target();
  }
}
