int main(void) {
  char carr[5] = {1, 2, 3, 4, 5};
  short sarr[5] = {10, 20, 30, 40, 50};
  int iarr[5] = {100, 200, 300, 400, 500};

  // Subscripting arrays directly
  if (carr[1] != 2) return 1;
  if (sarr[2] != 30) return 2;
  if (iarr[3] != 400) return 3;

  // Pointer assignment and subscripting
  char *cptr;
  cptr = carr;
  int *iptr;
  iptr = iarr;

  if (cptr[1] != 2) return 4;
  if (iptr[3] != 400) return 5;

  // Pointer arithmetic
  if (*(cptr + 2) != 3) return 6;
  if (*(iptr + 2) != 300) return 7;

  // Modifying through pointers/indexing
  cptr[1] = 9;
  iptr[3] = 900;
  if (carr[1] != 9) return 8;
  if (iarr[3] != 900) return 9;

  return 42;
}
