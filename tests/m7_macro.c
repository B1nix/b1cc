#define VALUE 42
#define SUM(a,b) a+b // note: b1cc macro doesn't parse formal params, but object-like macros are replaced textually!

int main(void) {
  int x = VALUE;
  return x;
}
