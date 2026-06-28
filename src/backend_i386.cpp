#include "backend_target.h"
#include "diagnostics.h"
#include <sstream>

namespace Backend {
  using namespace IR;

  class I386Target : public TargetBackend {
  public:
    std::string emit_globals(const std::vector<IrGlobalVar> &globals) override {
      std::ostringstream out;
      if (globals.empty())
        return "";
      out << ".data\n";
      for (const auto &g : globals) {
        if (!g.is_static) {
          out << ".globl " << g.name << "\n";
        }
        int i386_align = g.align;
        if (i386_align == 0) {
          i386_align = (g.elem_size >= 4) ? 2 : (g.elem_size == 2) ? 1 : 0;
        }
        out << ".type " << g.name << ", @object\n";
        long total_bytes = g.is_array ? (g.size * g.elem_size) : (g.initializers.empty() ? 1 : g.initializers.size()) * g.elem_size;
        out << ".size " << g.name << ", " << total_bytes << "\n";
        out << ".p2align " << i386_align << "\n";
        out << g.name << ":\n";
        if (g.is_array || g.initializers.size() > 1) {
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
                   inst.op == "u<" || inst.op == "u>" || inst.op == "u<=" ||
                   inst.op == "u>=" || inst.op == "u>>" ||
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
          else if (inst.op == "<<" || inst.op == ">>" || inst.op == "u>>") {
            if (inst.op == "<<")
              out << "    shll %cl, %eax\n";
            else if (inst.op == ">>")
              out << "    sarl %cl, %eax\n";
            else
              out << "    shrl %cl, %eax\n";
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
            else if (inst.op == ">=")
              out << "    setge %al\n";
            else if (inst.op == "u<")
              out << "    setb %al\n";
            else if (inst.op == "u>")
              out << "    seta %al\n";
            else if (inst.op == "u<=")
              out << "    setbe %al\n";
            else
              out << "    setae %al\n";
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
          if (global_var_elem_scales.count(inst.arg))
            gsize = global_var_elem_scales[inst.arg];
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
          if (global_var_elem_scales.count(inst.arg))
            gsize = global_var_elem_scales[inst.arg];
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
  };

  TargetBackend* create_i386_backend() {
    return new I386Target();
  }
}
