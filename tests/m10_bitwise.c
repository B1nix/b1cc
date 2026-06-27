int main(void) {
  // Test 1: Bitwise AND
  int and_val = 10 & 6;
  if (and_val != 2) {
    return 1;
  }
  
  // Test 2: Bitwise OR
  int or_val = 10 | 6;
  if (or_val != 14) {
    return 2;
  }
  
  // Test 3: Bitwise XOR
  int xor_val = 10 ^ 6;
  if (xor_val != 12) {
    return 3;
  }
  
  // Test 4: Shift left
  int shl_val = 2 << 3;
  if (shl_val != 16) {
    return 4;
  }
  
  // Test 5: Shift right
  int shr_val = 16 >> 2;
  if (shr_val != 4) {
    return 5;
  }
  
  // Test 6: Unary bitwise NOT
  int not_val = ~10;
  if (not_val != 0 - 11) {
    return 6;
  }
  
  // Test 7: Unary minus (negation)
  int neg_val = -10;
  if (neg_val != 0 - 10) {
    return 7;
  }
  
  // Test 8: Unary logical NOT
  if (!10) {
    return 8;
  }
  
  if (!0) {
    // ok
  } else {
    return 9;
  }
  
  // Test 9: Operator precedence (shifts have higher precedence than relational ops)
  // 1 << 3 > 5  must parse as (1 << 3) > 5 -> 8 > 5 -> 1.
  // If it parsed as 1 << (3 > 5) -> 1 << 0 -> 1.
  // Let's test: 1 << 3 < 5 -> (1 << 3) < 5 -> 8 < 5 -> 0.
  // If it parsed wrong as 1 << (3 < 5) -> 1 << 1 -> 2.
  int prec_test = 1 << 3 < 5;
  if (prec_test != 0) {
    return 10;
  }
  
  return 42;
}
