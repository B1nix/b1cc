#include "backend.h"
#include "lexer.h"
#include "parser.h"
#include "diagnostics.h"
#include <sstream>

namespace Backend {
  using namespace IR;

  static bool is_defined_global(const std::string &name) {
    for (const auto &g : global_decls) {
      if (g.name == name)
        return true;
    }
    return false;
  }

  static std::string emit_globals(const std::string &target) {
    std::ostringstream out;
    bool has_data = !global_decls.empty();
    if (!has_data)
      return "";
    out << ".data\n";
    for (const auto &g : global_decls) {
      if (!g.is_static) {
        out << ".globl " << (target == "arm64-darwin" ? "_" : "") << g.name << "\n";
      }
      int align = (g.elem_size == 8) ? 3 : (g.elem_size == 4) ? 2 : (g.elem_size == 2) ? 1 : 0;
      if (target == "arm64-darwin") {
        out << ".p2align " << align << "\n";
        out << "_" << g.name << ":\n";
      } else if (target == "x86_64-b1nix") {
        out << ".type " << g.name << ", @object\n";
        long total_bytes = g.is_array ? (g.size * g.elem_size) : g.elem_size;
        out << ".size " << g.name << ", " << total_bytes << "\n";
        out << ".p2align " << align << "\n";
        out << g.name << ":\n";
      } else {
        int i386_align = (g.elem_size >= 4) ? 2 : (g.elem_size == 2) ? 1 : 0;
        out << ".type " << g.name << ", @object\n";
        long total_bytes = g.is_array ? (g.size * g.elem_size) : g.elem_size;
        out << ".size " << g.name << ", " << total_bytes << "\n";
        out << ".p2align " << i386_align << "\n";
        out << g.name << ":\n";
      }
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

  static std::string emit_arm64_darwin(const IrFunction &fn) {
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
          // stack argument at [x29, #16 + (i - 8) * 8]
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
          // Sign-extend/truncate to target byte size
          if (inst.value == 1)
            out << "    sxtb x0, w0\n";
          else if (inst.value == 2)
            out << "    sxth x0, w0\n";
          else if (inst.value == 4)
            out << "    sxtw x0, w0\n";
          // else 8-byte: no-op
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
        if (IR::global_var_elem_scales.count(inst.arg)) {
          gsize = IR::global_var_elem_scales[inst.arg];
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
        if (IR::global_var_elem_scales.count(inst.arg)) {
          gsize = IR::global_var_elem_scales[inst.arg];
        }
        if (is_defined_global(inst.arg)) {
          out << "    adrp x1, _" << inst.arg << "@PAGE\n";
          if (gsize == 1) {
            out << "    strb w0, [x1, _" << inst.arg << "@PAGEOFF]\n";
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

  static std::string emit_x86_64_b1nix(const IrFunction &fn) {
    static const char *regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    std::ostringstream out;
    if (!fn.strings.empty()) {
      out << ".section .rodata\n";
      for (const auto &s : fn.strings)
        out << s.first << ":\n    .asciz " << s.second << "\n";
    }
    out << ".text\n";
    if (!fn.is_static) {
      out << ".globl " << fn.name << "\n";
    }
    out << ".type " << fn.name << ", @function\n";
    out << fn.name << ":\n";
    const bool frame = fn.has_call || !fn.locals.empty();
    if (frame) {
      out << "    pushq %rbp\n";
      out << "    movq %rsp, %rbp\n";
      if (!fn.locals.empty())
        out << "    subq $" << (fn.locals.size() * 16) << ", %rsp\n";
      static const char *arg_regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
      for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i < 6) {
          out << "    movq " << arg_regs[i] << ", -" << ((i + 1) * 16) << "(%rbp)\n";
        } else {
          // stack argument at [16 + (i - 6) * 8](%rbp)
          out << "    movq " << (16 + (i - 6) * 8) << "(%rbp), %rax\n";
          out << "    movq %rax, -" << ((i + 1) * 16) << "(%rbp)\n";
        }
      }
    }
    for (const IrInst &inst : fn.code) {
      if (inst.op == "const") {
        out << "    movq $" << inst.value << ", %rax\n";
        out << "    pushq %rax\n";
      } else if (inst.op == "load") {
        out << "    movq -" << ((inst.value + 1) * 16) << "(%rbp), %rax\n";
        out << "    pushq %rax\n";
      } else if (inst.op == "str") {
        out << "    leaq " << inst.arg << "(%rip), %rax\n";
        out << "    pushq %rax\n";
      } else if (inst.op == "store") {
        out << "    popq %rax\n";
        out << "    movq %rax, -" << ((inst.value + 1) * 16) << "(%rbp)\n";
      } else if (inst.op == "pop") {
        out << "    addq $8, %rsp\n";
      } else if (inst.op == "+" || inst.op == "-" || inst.op == "*" ||
                 inst.op == "/" || inst.op == "%" || inst.op == "==" || inst.op == "!=" ||
                 inst.op == "<" || inst.op == ">" || inst.op == "<=" ||
                 inst.op == ">=" || inst.op == "index" ||
                 inst.op == "&" || inst.op == "|" || inst.op == "^" ||
                 inst.op == "<<" || inst.op == ">>") {
        out << "    popq %rcx\n";
        out << "    popq %rax\n";
        if (inst.op == "index") {
          if (inst.value == 1) {
            out << "    movsbq (%rax,%rcx,1), %rax\n";
          } else if (inst.value == 2) {
            out << "    movswq (%rax,%rcx,2), %rax\n";
          } else if (inst.value == 4) {
            out << "    movslq (%rax,%rcx,4), %rax\n";
          } else {
            out << "    movq (%rax,%rcx,8), %rax\n";
          }
        } else if (inst.op == "+")
          out << "    addq %rcx, %rax\n";
        else if (inst.op == "-")
          out << "    subq %rcx, %rax\n";
        else if (inst.op == "*")
          out << "    imulq %rcx, %rax\n";
        else if (inst.op == "/") {
          out << "    cqto\n";
          out << "    idivq %rcx\n";
        } else if (inst.op == "%") {
          out << "    cqto\n";
          out << "    idivq %rcx\n";
          out << "    movq %rdx, %rax\n";
        } else if (inst.op == "&")
          out << "    andq %rcx, %rax\n";
        else if (inst.op == "|")
          out << "    orq %rcx, %rax\n";
        else if (inst.op == "^")
          out << "    xorq %rcx, %rax\n";
        else if (inst.op == "<<" || inst.op == ">>") {
          if (inst.op == "<<")
            out << "    shlq %cl, %rax\n";
          else
            out << "    sarq %cl, %rax\n";
        } else {
          out << "    cmpq %rcx, %rax\n";
          if (inst.op == "==")
            out << "    sete %al\n";
          else if (inst.op == "!=")
            out << "    setne %al\n";
          else if (inst.op == "<")
            out << "    setl %al\n";
          else if (inst.op == ">")
            out << "    setg %al\n";
          else if (inst.op == "<=")
            out << "    setle %al\n";
          else
            out << "    setge %al\n";
          out << "    movzbq %al, %rax\n";
        }
        out << "    pushq %rax\n";
      } else if (inst.op == "~" || inst.op == "!" || inst.op == "neg" || inst.op == "cast") {
        out << "    popq %rax\n";
        if (inst.op == "~")
          out << "    notq %rax\n";
        else if (inst.op == "neg")
          out << "    negq %rax\n";
        else if (inst.op == "cast") {
          if (inst.value == 1)
            out << "    movsbq %al, %rax\n";
          else if (inst.value == 2)
            out << "    movswq %ax, %rax\n";
          else if (inst.value == 4)
            out << "    movslq %eax, %rax\n";
          // else 8-byte: no-op
        } else {
          out << "    cmpq $0, %rax\n";
          out << "    sete %al\n";
          out << "    movzbq %al, %rax\n";
        }
        out << "    pushq %rax\n";
      } else if (inst.op == "jz") {
        out << "    popq %rax\n";
        out << "    cmpq $0, %rax\n";
        out << "    je " << inst.arg << "\n";
      } else if (inst.op == "jmp") {
        out << "    jmp " << inst.arg << "\n";
      } else if (inst.op == "label") {
        out << inst.arg << ":\n";
      } else if (inst.op == "call" || inst.op == "icall") {
        long num_args = inst.value;
        if (num_args <= 6) {
          for (long i = num_args - 1; i >= 0; --i)
            out << "    popq " << regs[i] << "\n";
          if (inst.op == "icall") {
            out << "    popq %rax\n";
            out << "    movq %rax, %r11\n";
            out << "    xorl %eax, %eax\n";
            out << "    call *%r11\n";
          } else {
            out << "    xorl %eax, %eax\n";
            out << "    call " << inst.arg << "\n";
          }
        } else {
          long num_stack_args = num_args - 6;
          static const char *temp_regs[] = {"%r10", "%r11", "%r12", "%r13", "%r14", "%r15"};
          for (long i = num_args - 1; i >= 6; --i) {
            int t_idx = (int)(i - 6);
            out << "    popq " << temp_regs[t_idx % 6] << "\n";
          }
          for (long i = 5; i >= 0; --i) {
            out << "    popq " << regs[i] << "\n";
          }
          if (inst.op == "icall") {
            out << "    popq %rax\n";
          }
          for (long i = num_args - 1; i >= 6; --i) {
            int t_idx = (int)(i - 6);
            out << "    pushq " << temp_regs[t_idx % 6] << "\n";
          }
          if (inst.op == "icall") {
            out << "    movq %rax, %r10\n";
            out << "    xorl %eax, %eax\n";
            out << "    call *%r10\n";
          } else {
            out << "    xorl %eax, %eax\n";
            out << "    call " << inst.arg << "\n";
          }
          out << "    addq $" << (num_stack_args * 8) << ", %rsp\n";
        }
        out << "    pushq %rax\n";
      } else if (inst.op == "addr") {
        out << "    leaq -" << ((inst.value + 1) * 16) << "(%rbp), %rax\n";
        out << "    pushq %rax\n";
      } else if (inst.op == "gload") {
        int gsize = 8;
        if (IR::global_var_elem_scales.count(inst.arg))
          gsize = IR::global_var_elem_scales[inst.arg];
        if (gsize == 1)
          out << "    movsbl " << inst.arg << "(%rip), %eax\n";
        else if (gsize == 2)
          out << "    movswq " << inst.arg << "(%rip), %rax\n";
        else if (gsize == 4)
          out << "    movslq " << inst.arg << "(%rip), %rax\n";
        else
          out << "    movq " << inst.arg << "(%rip), %rax\n";
        out << "    pushq %rax\n";
      } else if (inst.op == "gstore") {
        out << "    popq %rax\n";
        int gsize = 8;
        if (IR::global_var_elem_scales.count(inst.arg))
          gsize = IR::global_var_elem_scales[inst.arg];
        if (gsize == 1)
          out << "    movb %al, " << inst.arg << "(%rip)\n";
        else if (gsize == 2)
          out << "    movw %ax, " << inst.arg << "(%rip)\n";
        else if (gsize == 4)
          out << "    movl %eax, " << inst.arg << "(%rip)\n";
        else
          out << "    movq %rax, " << inst.arg << "(%rip)\n";
      } else if (inst.op == "gaddr") {
        out << "    leaq " << inst.arg << "(%rip), %rax\n";
        out << "    pushq %rax\n";
      } else if (inst.op == "store_index") {
        out << "    popq %rax\n";
        out << "    popq %rcx\n";
        out << "    popq %rdx\n";
        if (inst.value == 1) {
          out << "    movb %dl, (%rax,%rcx,1)\n";
        } else if (inst.value == 2) {
          out << "    movw %dx, (%rax,%rcx,2)\n";
        } else if (inst.value == 4) {
          out << "    movl %edx, (%rax,%rcx,4)\n";
        } else {
          out << "    movq %rdx, (%rax,%rcx,8)\n";
        }
      } else if (inst.op == "ret") {
        out << "    popq %rax\n";
        if (frame)
          out << "    leave\n";
        out << "    ret\n";
      } else {
        Diagnostics::fatal("unknown IR op " + inst.op);
      }
    }
    out << ".size " << fn.name << ", .-" << fn.name << "\n";
    return out.str();
  }

  static std::string emit_i386_b1nix(const IrFunction &fn) {
    std::ostringstream out;

    if (!fn.strings.empty()) {
      out << ".section .rodata\n";
      for (const auto &s : fn.strings)
        out << s.first << ":\n    .asciz " << s.second << "\n";
    }
    out << ".text\n";
    if (!fn.is_static) {
      out << ".globl " << fn.name << "\n";
    }
    out << ".type " << fn.name << ", @function\n";
    out << fn.name << ":\n";
    const bool frame = fn.has_call || !fn.locals.empty();
    if (frame) {
      out << "    pushl %ebp\n";
      out << "    movl %esp, %ebp\n";
      if (!fn.locals.empty())
        out << "    subl $" << (fn.locals.size() * 16) << ", %esp\n";
      for (size_t i = 0; i < fn.params.size(); ++i) {
        out << "    movl " << (8 + i * 4) << "(%ebp), %eax\n";
        out << "    movl %eax, -" << ((i + 1) * 16) << "(%ebp)\n";
      }
    }
    for (const IrInst &inst : fn.code) {
      if (inst.op == "const") {
        out << "    movl $" << inst.value << ", %eax\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "load") {
        out << "    movl -" << ((inst.value + 1) * 16) << "(%ebp), %eax\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "str") {
        out << "    movl $" << inst.arg << ", %eax\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "store") {
        out << "    popl %eax\n";
        out << "    movl %eax, -" << ((inst.value + 1) * 16) << "(%ebp)\n";
      } else if (inst.op == "pop") {
        out << "    addl $4, %esp\n";
      } else if (inst.op == "+" || inst.op == "-" || inst.op == "*" ||
                 inst.op == "/" || inst.op == "%" || inst.op == "==" || inst.op == "!=" ||
                 inst.op == "<" || inst.op == ">" || inst.op == "<=" ||
                 inst.op == ">=" || inst.op == "index" ||
                 inst.op == "&" || inst.op == "|" || inst.op == "^" ||
                 inst.op == "<<" || inst.op == ">>") {
        out << "    popl %ecx\n";
        out << "    popl %eax\n";
        if (inst.op == "index") {
          if (inst.value == 1) {
            out << "    movsbl (%eax,%ecx,1), %eax\n";
          } else if (inst.value == 2) {
            out << "    movswl (%eax,%ecx,2), %eax\n";
          } else if (inst.value == 4) {
            out << "    movl (%eax,%ecx,4), %eax\n";
          } else {
            out << "    movl (%eax,%ecx,8), %eax\n";
          }
        } else if (inst.op == "+")
          out << "    addl %ecx, %eax\n";
        else if (inst.op == "-")
          out << "    subl %ecx, %eax\n";
        else if (inst.op == "*")
          out << "    imull %ecx, %eax\n";
        else if (inst.op == "/") {
          out << "    cltd\n";
          out << "    idivl %ecx\n";
        } else if (inst.op == "%") {
          out << "    cltd\n";
          out << "    idivl %ecx\n";
          out << "    movl %edx, %eax\n";
        } else if (inst.op == "&")
          out << "    andl %ecx, %eax\n";
        else if (inst.op == "|")
          out << "    orl %ecx, %eax\n";
        else if (inst.op == "^")
          out << "    xorl %ecx, %eax\n";
        else if (inst.op == "<<" || inst.op == ">>") {
          if (inst.op == "<<")
            out << "    shll %cl, %eax\n";
          else
            out << "    sarl %cl, %eax\n";
        } else {
          out << "    cmpl %ecx, %eax\n";
          if (inst.op == "==")
            out << "    sete %al\n";
          else if (inst.op == "!=")
            out << "    setne %al\n";
          else if (inst.op == "<")
            out << "    setl %al\n";
          else if (inst.op == ">")
            out << "    setg %al\n";
          else if (inst.op == "<=")
            out << "    setle %al\n";
          else
            out << "    setge %al\n";
          out << "    movzbl %al, %eax\n";
        }
        out << "    pushl %eax\n";
      } else if (inst.op == "~" || inst.op == "!" || inst.op == "neg" || inst.op == "cast") {
        out << "    popl %eax\n";
        if (inst.op == "~")
          out << "    notl %eax\n";
        else if (inst.op == "neg")
          out << "    negl %eax\n";
        else if (inst.op == "cast") {
          if (inst.value == 1)
            out << "    movsbl %al, %eax\n";
          else if (inst.value == 2)
            out << "    movswl %ax, %eax\n";
          // else 4-byte or 8-byte: no-op on i386
        } else {
          out << "    cmpl $0, %eax\n";
          out << "    sete %al\n";
          out << "    movzbl %al, %eax\n";
        }
        out << "    pushl %eax\n";
      } else if (inst.op == "jz") {
        out << "    popl %eax\n";
        out << "    cmpl $0, %eax\n";
        out << "    je " << inst.arg << "\n";
      } else if (inst.op == "jmp") {
        out << "    jmp " << inst.arg << "\n";
      } else if (inst.op == "label") {
        out << inst.arg << ":\n";
      } else if (inst.op == "call") {
        long num_args = inst.value;
        out << "    subl $" << (num_args * 4) << ", %esp\n";
        for (long i = 0; i < num_args; ++i) {
          out << "    movl " << ((2 * num_args - 1 - i) * 4) << "(%esp), %eax\n";
          out << "    movl %eax, " << (i * 4) << "(%esp)\n";
        }
        out << "    call " << inst.arg << "\n";
        out << "    addl $" << (2 * num_args * 4) << ", %esp\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "icall") {
        long num_args = inst.value;
        out << "    subl $" << (num_args * 4) << ", %esp\n";
        for (long i = 0; i < num_args; ++i) {
          out << "    movl " << ((2 * num_args - 1 - i) * 4) << "(%esp), %eax\n";
          out << "    movl %eax, " << (i * 4) << "(%esp)\n";
        }
        out << "    movl " << ((2 * num_args) * 4) << "(%esp), %eax\n";
        out << "    call *%eax\n";
        out << "    addl $" << ((2 * num_args + 1) * 4) << ", %esp\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "addr") {
        out << "    leal -" << ((inst.value + 1) * 16) << "(%ebp), %eax\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "gload") {
        int gsize = 4;
        if (IR::global_var_elem_scales.count(inst.arg))
          gsize = IR::global_var_elem_scales[inst.arg];
        if (gsize == 1)
          out << "    movsbl " << inst.arg << ", %eax\n";
        else if (gsize == 2)
          out << "    movswl " << inst.arg << ", %eax\n";
        else
          out << "    movl " << inst.arg << ", %eax\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "gstore") {
        out << "    popl %eax\n";
        int gsize = 4;
        if (IR::global_var_elem_scales.count(inst.arg))
          gsize = IR::global_var_elem_scales[inst.arg];
        if (gsize == 1)
          out << "    movb %al, " << inst.arg << "\n";
        else if (gsize == 2)
          out << "    movw %ax, " << inst.arg << "\n";
        else
          out << "    movl %eax, " << inst.arg << "\n";
      } else if (inst.op == "gaddr") {
        out << "    movl $" << inst.arg << ", %eax\n";
        out << "    pushl %eax\n";
      } else if (inst.op == "store_index") {
        out << "    popl %eax\n";
        out << "    popl %ecx\n";
        out << "    popl %edx\n";
        if (inst.value == 1) {
          out << "    movb %dl, (%eax,%ecx,1)\n";
        } else if (inst.value == 2) {
          out << "    movw %dx, (%eax,%ecx,2)\n";
        } else if (inst.value == 4) {
          out << "    movl %edx, (%eax,%ecx,4)\n";
        } else {
          out << "    movl %edx, (%eax,%ecx,8)\n";
        }
      } else if (inst.op == "ret") {
        out << "    popl %eax\n";
        if (frame)
          out << "    leave\n";
        out << "    ret\n";
      } else {
        Diagnostics::fatal("unknown IR op " + inst.op);
      }
    }
    out << ".size " << fn.name << ", .-" << fn.name << "\n";
    return out.str();
  }

  static void dump_ast_node(const AST::Node &node, int indent) {
    std::string ind(indent * 2, ' ');
    std::cerr << ind << "Node(op=" << node.op;
    if (!node.name.empty()) std::cerr << ", name=" << node.name;
    if (node.value != 0) std::cerr << ", value=" << node.value;
    std::cerr << ")\n";
    if (node.lhs) dump_ast_node(*node.lhs, indent + 1);
    if (node.rhs) dump_ast_node(*node.rhs, indent + 1);
    for (const auto &child : node.body) {
      if (child) dump_ast_node(*child, indent + 1);
    }
  }

  static void dump_ast(const std::vector<std::unique_ptr<AST::Node>> &ast) {
    std::cerr << "=== AST DUMP ===\n";
    for (const auto &node : ast) {
      if (node) dump_ast_node(*node, 0);
    }
    std::cerr << "================\n";
  }

  static void dump_ir(const std::vector<IrFunction> &funcs) {
    std::cerr << "=== IR DUMP ===\n";
    for (const auto &fn : funcs) {
      std::cerr << "Function " << fn.name << ":\n";
      for (const auto &inst : fn.code) {
        std::cerr << "  " << inst.op;
        if (!inst.arg.empty()) std::cerr << " " << inst.arg;
        if (inst.value != 0) std::cerr << " " << inst.value;
        std::cerr << "\n";
      }
    }
    std::cerr << "================\n";
  }

  std::string compile_asm(const std::string &src, const std::string &target, bool dump_ast_flag, bool dump_ir_flag) {
    global_decls.clear();
    global_vars.clear();
    global_arrays.clear();
    global_array_dims.clear();
    local_array_dims.clear();
    global_array_base_sizes.clear();
    local_array_base_sizes.clear();
    global_var_elem_scales.clear();
    global_var_is_pointer.clear();
    local_var_elem_scales.clear();
    local_var_is_pointer.clear();

    int target_scale = (target == "i386-b1nix" || target == "x86-b1nix") ? 4 : 8;
    std::map<std::string, Preprocessor::Macro> macros = Preprocessor::driver_macros;
    std::set<std::string> included_files;
    included_files.insert(Diagnostics::filepath);
    std::string preprocessed_src = Preprocessor::preprocess(src, Diagnostics::filepath, Preprocessor::driver_include_dirs, macros, included_files);

    std::ostringstream out;
    auto tokens = Lexer::lex(preprocessed_src, macros);
    Parser::Parser parser(std::move(tokens), target_scale);
    auto ast = parser.parse();
    if (dump_ast_flag) {
      dump_ast(ast);
    }
    auto ir_functions = lower_program(std::move(ast), target);
    if (dump_ir_flag) {
      dump_ir(ir_functions);
    }

    out << emit_globals(target);

    for (const IrFunction &fn : ir_functions) {
      if (target == "arm64-darwin")
        out << emit_arm64_darwin(fn);
      else if (target == "x86_64-b1nix")
        out << emit_x86_64_b1nix(fn);
      else if (target == "i386-b1nix" || target == "x86-b1nix")
        out << emit_i386_b1nix(fn);
      else
        Diagnostics::fatal("unknown target " + target);
    }
    return out.str();
  }
}
