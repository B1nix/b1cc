int main(void) {
  int i = 0;
  
  // Test 1: Postfix ++
  int val1 = i++;
  if (val1 != 0) {
    return 1;
  }
  if (i != 1) {
    return 2;
  }
  
  // Test 2: Prefix ++
  int val2 = ++i;
  if (val2 != 2) {
    return 3;
  }
  if (i != 2) {
    return 4;
  }
  
  // Test 3: Postfix --
  int val3 = i--;
  if (val3 != 2) {
    return 5;
  }
  if (i != 1) {
    return 6;
  }
  
  // Test 4: Prefix --
  int val4 = --i;
  if (val4 != 0) {
    return 7;
  }
  if (i != 0) {
    return 8;
  }
  
  // Test 5: Compound +=
  i += 5;
  if (i != 5) {
    return 9;
  }
  
  // Test 6: Compound -=
  i -= 2;
  if (i != 3) {
    return 10;
  }
  
  // Test 7: Compound *=
  i *= 4;
  if (i != 12) {
    return 11;
  }
  
  // Test 8: Compound /=
  i /= 3;
  if (i != 4) {
    return 12;
  }
  
  // Test 9: Loop using postfix ++
  int sum = 0;
  int k;
  for (k = 0; k < 5; k++) {
    sum += k;
  }
  if (sum != 10) {
    return 13;
  }
  
  return 42;
}
