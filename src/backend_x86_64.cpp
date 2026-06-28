#include "backend_target.h"
#include "diagnostics.h"
#include <sstream>

namespace Backend {
  using namespace IR;

  class X86_64Target : public TargetBackend {
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
        int align = g.align;
        if (align == 0) {
          align = (g.elem_size == 8) ? 3 : (g.elem_size == 4) ? 2 : (g.elem_size == 2) ? 1 : 0;
        }
        out << ".type " << g.name << ", @object\n";
        long total_bytes = g.is_array ? (g.size * g.elem_size) : (g.initializers.empty() ? 1 : g.initializers.size()) * g.elem_size;
        out << ".size " << g.name << ", " << total_bytes << "\n";
        out << ".p2align " << align << "\n";
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
        int abi_word = 0;
        int stack_word = 0;
        for (size_t i = 0; i < fn.params.size(); ++i) {
          int agg_size = (i < fn.param_aggregate_sizes.size()) ? fn.param_aggregate_sizes[i] : 0;
          int words = agg_size > 0 ? (agg_size + 7) / 8 : 1;
          bool in_regs = abi_word + words <= 6;
          for (int w = 0; w < words; ++w) {
            int off = -((int)(i + 1) * 16) + (w * 8);
            if (in_regs) {
              out << "    movq " << arg_regs[abi_word + w] << ", " << off << "(%rbp)\n";
            } else {
              out << "    movq " << (16 + stack_word * 8) << "(%rbp), %rax\n";
              out << "    movq %rax, " << off << "(%rbp)\n";
              stack_word++;
            }
          }
          if (in_regs) {
            abi_word += words;
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
                   inst.op == "u<" || inst.op == "u>" || inst.op == "u<=" ||
                   inst.op == "u>=" || inst.op == "u>>" ||
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
          else if (inst.op == "<<" || inst.op == ">>" || inst.op == "u>>") {
            if (inst.op == "<<")
              out << "    shlq %cl, %rax\n";
            else if (inst.op == ">>")
              out << "    sarq %cl, %rax\n";
            else
              out << "    shrq %cl, %rax\n";
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
          std::vector<int> agg_sizes;
          if (inst.op == "call" && function_param_aggregate_sizes.count(inst.arg)) {
            agg_sizes = function_param_aggregate_sizes[inst.arg];
          }
          bool has_aggregate_arg = false;
          std::vector<bool> arg_in_regs(num_args, false);
          std::vector<int> arg_first_word(num_args, 0);
          std::vector<int> arg_stack_word(num_args, 0);
          int abi_words = 0;
          int stack_words = 0;
          for (long i = 0; i < num_args; ++i) {
            int agg_size = (i < (long)agg_sizes.size()) ? agg_sizes[i] : 0;
            int words = agg_size > 0 ? (agg_size + 7) / 8 : 1;
            has_aggregate_arg = has_aggregate_arg || agg_size > 0;
            if (abi_words + words <= 6) {
              arg_in_regs[i] = true;
              arg_first_word[i] = abi_words;
              abi_words += words;
            } else {
              arg_stack_word[i] = stack_words;
              stack_words += words;
            }
          }
          if (has_aggregate_arg) {
            int stack_bytes = stack_words * 8;
            if (stack_bytes > 0)
              out << "    subq $" << stack_bytes << ", %rsp\n";
            for (long i = 0; i < num_args; ++i) {
              int agg_size = (i < (long)agg_sizes.size()) ? agg_sizes[i] : 0;
              int words = agg_size > 0 ? (agg_size + 7) / 8 : 1;
              int src_off = stack_bytes + (int)(num_args - 1 - i) * 8;
              if (agg_size > 0) {
                out << "    movq " << src_off << "(%rsp), %r10\n";
                for (int w = 0; w < words; ++w) {
                  if (arg_in_regs[i]) {
                    out << "    movq " << (w * 8) << "(%r10), " << regs[arg_first_word[i] + w] << "\n";
                  } else {
                    out << "    movq " << (w * 8) << "(%r10), %r11\n";
                    out << "    movq %r11, " << ((arg_stack_word[i] + w) * 8) << "(%rsp)\n";
                  }
                }
              } else {
                if (arg_in_regs[i]) {
                  out << "    movq " << src_off << "(%rsp), " << regs[arg_first_word[i]] << "\n";
                } else {
                  out << "    movq " << src_off << "(%rsp), %r11\n";
                  out << "    movq %r11, " << (arg_stack_word[i] * 8) << "(%rsp)\n";
                }
              }
            }
            if (inst.op == "icall") {
              out << "    movq " << (stack_bytes + num_args * 8) << "(%rsp), %rax\n";
              out << "    movq %rax, %r11\n";
              out << "    xorl %eax, %eax\n";
              out << "    call *%r11\n";
            } else {
              out << "    xorl %eax, %eax\n";
              out << "    call " << inst.arg << "\n";
            }
            out << "    addq $" << (stack_bytes + num_args * 8 + (inst.op == "icall" ? 8 : 0)) << ", %rsp\n";
          } else if (num_args <= 6) {
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
          int ret_agg_size = (inst.op == "call" && function_return_aggregate_sizes.count(inst.arg)) ? function_return_aggregate_sizes[inst.arg] : 0;
          out << "    pushq %rax\n";
          if (ret_agg_size > 8)
            out << "    pushq %rdx\n";
        } else if (inst.op == "addr") {
          out << "    leaq -" << ((inst.value + 1) * 16) << "(%rbp), %rax\n";
          out << "    pushq %rax\n";
        } else if (inst.op == "gload") {
          int gsize = 8;
          if (global_var_elem_scales.count(inst.arg))
            gsize = global_var_elem_scales[inst.arg];
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
          if (global_var_elem_scales.count(inst.arg))
            gsize = global_var_elem_scales[inst.arg];
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
        } else if (inst.op == "store_agg") {
          out << "    popq %r10\n";
          if (inst.value > 8) {
            out << "    popq %rax\n";
            out << "    movq %rax, 8(%r10)\n";
          }
          out << "    popq %rax\n";
          out << "    movq %rax, (%r10)\n";
        } else if (inst.op == "ret_agg") {
          out << "    popq %r10\n";
          out << "    movq (%r10), %rax\n";
          if (inst.value > 8)
            out << "    movq 8(%r10), %rdx\n";
          if (frame)
            out << "    leave\n";
          out << "    ret\n";
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
  };

  TargetBackend* create_x86_64_backend() {
    return new X86_64Target();
  }
}
