#include <stdio.h>
#include <stdlib.h>

#define STATUS_OK 0

int sign_extend(int val) {
  if (val > 2147483647) {
    return val - 4294967296;
  }
  return val;
}

int is_space(char c) {
  if (c == 32) return 1;
  if (c == 9) return 1;
  if (c == 10) return 1;
  if (c == 13) return 1;
  return 0;
}

int is_digit(char c) {
  if (c >= 48) {
    if (c <= 57) {
      return 1;
    }
  }
  return 0;
}

int is_alpha(char c) {
  if (c >= 97) {
    if (c <= 122) {
      return 1;
    }
  }
  if (c >= 65) {
    if (c <= 90) {
      return 1;
    }
  }
  if (c == 95) {
    return 1;
  }
  return 0;
}

int is_alnum(char c) {
  if (is_alpha(c)) return 1;
  if (is_digit(c)) return 1;
  return 0;
}

int main(void) {
  int c;
  c = sign_extend(getchar());
  
  while (c != 0 - 1) {
    if (is_space(c)) {
      c = sign_extend(getchar());
    } else if (c == 35) {
      int loop1 = 1;
      while (loop1) {
        if (c == 0 - 1) {
          loop1 = 0;
        } else {
          if (c == 10) {
            loop1 = 0;
          } else {
            c = sign_extend(getchar());
          }
        }
      }
    } else if (c == 47) {
      int next;
      next = sign_extend(getchar());
      if (next == 47) {
        int loop2 = 1;
        while (loop2) {
          if (c == 0 - 1) {
            loop2 = 0;
          } else {
            if (c == 10) {
              loop2 = 0;
            } else {
              c = sign_extend(getchar());
            }
          }
        }
      } else if (next == 42) {
        int in_comment = 1;
        c = sign_extend(getchar());
        while (in_comment) {
          if (c == 0 - 1) {
            in_comment = 0;
          } else {
            if (c == 42) {
              int after;
              after = sign_extend(getchar());
              if (after == 47) {
                c = sign_extend(getchar());
                in_comment = 0;
              } else {
                c = after;
              }
            } else {
              c = sign_extend(getchar());
            }
          }
        }
      } else {
        printf("OP: /\n");
        c = next;
      }
    } else if (is_digit(c)) {
      printf("NUM: ");
      int loop4 = 1;
      while (loop4) {
        if (c == 0 - 1) {
          loop4 = 0;
        } else {
          if (is_digit(c)) {
            putchar(c);
            c = sign_extend(getchar());
          } else {
            loop4 = 0;
          }
        }
      }
      putchar(10);
    } else if (is_alpha(c)) {
      printf("IDENT: ");
      int loop5 = 1;
      while (loop5) {
        if (c == 0 - 1) {
          loop5 = 0;
        } else {
          if (is_alnum(c)) {
            putchar(c);
            c = sign_extend(getchar());
          } else {
            loop5 = 0;
          }
        }
      }
      putchar(10);
    } else if (c == 34) {
      printf("STR: ");
      c = sign_extend(getchar());
      int loop6 = 1;
      while (loop6) {
        if (c == 0 - 1) {
          loop6 = 0;
        } else {
          if (c == 34) {
            loop6 = 0;
          } else {
            putchar(c);
            c = sign_extend(getchar());
          }
        }
      }
      putchar(10);
      if (c == 34) {
        c = sign_extend(getchar());
      }
    } else {
      printf("OP: ");
      putchar(c);
      putchar(10);
      c = sign_extend(getchar());
    }
  }
  
  printf("EOF\n");
  return STATUS_OK;
}
