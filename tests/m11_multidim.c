int main(void) {
  int a[2][3] = {1, 2, 3, 4, 5, 6};

  if (a[0][0] != 1) return 1;
  if (a[1][2] != 6) return 2;

  a[0][1] = 9;
  if (a[0][1] != 9) return 3;

  return 42;
}
