static int g = 40;

int bump(void) {
  static int x = 0;
  x += 1;
  return x;
}

int main(void) {
  if (bump() != 1) return 1;
  if (bump() != 2) return 2;
  g += 2;
  return g;
}
