#!/usr/bin/env python3
import argparse
import platform
import re
import subprocess
import sys
import tempfile
from pathlib import Path


TOKENS = re.compile(r"\s*(?:(\d+)|([A-Za-z_]\w*)|(==|!=|<=|>=|[{}();+\-*/]))")


class Parser:
    def __init__(self, text):
        self.tokens = self.lex(text)
        self.i = 0

    @staticmethod
    def lex(text):
        out = []
        pos = 0
        while pos < len(text):
            m = TOKENS.match(text, pos)
            if not m:
                if text[pos:].strip() == "":
                    break
                raise SyntaxError(f"unexpected character at {pos}: {text[pos]!r}")
            num, ident, punct = m.groups()
            out.append(num or ident or punct)
            pos = m.end()
        out.append("EOF")
        return out

    def peek(self):
        return self.tokens[self.i]

    def take(self, want=None):
        got = self.peek()
        if want is not None and got != want:
            raise SyntaxError(f"expected {want!r}, got {got!r}")
        self.i += 1
        return got

    def parse(self):
        self.take("int")
        self.take("main")
        self.take("(")
        if self.peek() == "void":
            self.take("void")
        self.take(")")
        self.take("{")
        self.take("return")
        expr = self.expr()
        self.take(";")
        self.take("}")
        self.take("EOF")
        return expr

    def expr(self):
        node = self.term()
        while self.peek() in ("+", "-"):
            op = self.take()
            node = (op, node, self.term())
        return node

    def term(self):
        node = self.factor()
        while self.peek() in ("*", "/"):
            op = self.take()
            node = (op, node, self.factor())
        return node

    def factor(self):
        tok = self.peek()
        if tok == "(":
            self.take("(")
            node = self.expr()
            self.take(")")
            return node
        if tok.isdigit():
            return ("num", int(self.take()))
        raise SyntaxError(f"expected expression, got {tok!r}")


def arm64_darwin(expr):
    lines = [".globl _main", ".p2align 2", "_main:"]

    def gen(node):
        kind = node[0]
        if kind == "num":
            lines.append(f"    mov x0, #{node[1]}")
            return
        op, lhs, rhs = node
        gen(lhs)
        lines.append("    str x0, [sp, #-16]!")
        gen(rhs)
        lines.append("    ldr x1, [sp], #16")
        if op == "+":
            lines.append("    add x0, x1, x0")
        elif op == "-":
            lines.append("    sub x0, x1, x0")
        elif op == "*":
            lines.append("    mul x0, x1, x0")
        elif op == "/":
            lines.append("    sdiv x0, x1, x0")

    gen(expr)
    lines.append("    ret")
    return "\n".join(lines) + "\n"


def compile_source(src):
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise SystemExit("b1cc: only Darwin ARM64 backend exists today")
    return arm64_darwin(Parser(src).parse())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("-S", action="store_true", help="emit assembly")
    ap.add_argument("-o", default="a.out")
    ns = ap.parse_args()

    asm = compile_source(Path(ns.input).read_text())
    if ns.S:
        Path(ns.o).write_text(asm)
        return 0

    with tempfile.NamedTemporaryFile("w", suffix=".s", delete=False) as f:
        f.write(asm)
        asm_path = f.name
    try:
        return subprocess.call(["cc", asm_path, "-o", ns.o])
    finally:
        Path(asm_path).unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
