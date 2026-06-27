# b1cc

Tiny clean-room C-subset compiler experiment for B1NIX.

Current scope:

- one function: `int main(void) { return expression; }`
- integer literals
- `+`, `-`, `*`, `/`, parentheses
- Darwin ARM64 assembly output on Apple Silicon

Run:

```sh
./test.sh
```

Compile manually:

```sh
./b1cc.py tests/return_42.c -S -o /tmp/return_42.s
cc /tmp/return_42.s -o /tmp/return_42
/tmp/return_42
echo $?
```

Skipped: full C, ELF, x86, linker, preprocessor. Add each only when a real B1NIX smoke needs it.
