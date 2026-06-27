int run_switch(int val) {
  int result = 0;
  switch (val) {
    case 1:
      result = 10;
      break;
    case 2:
    case 3:
      // test fall-through
      result = 20;
      break;
    case -5:
      result = 50;
      break;
    default:
      result = 99;
      break;
  }
  return result;
}

int main(void) {
  // Test 1: Simple case
  if (run_switch(1) != 10) {
    return 1;
  }
  
  // Test 2: Fall-through case
  if (run_switch(2) != 20) {
    return 2;
  }
  if (run_switch(3) != 20) {
    return 3;
  }
  
  // Test 3: Negative case
  if (run_switch(0 - 5) != 50) {
    return 4;
  }
  
  // Test 4: Default case
  if (run_switch(100) != 99) {
    return 5;
  }
  
  // Test 5: Loop break control
  int sum1 = 0;
  int i;
  for (i = 0; i < 10; i++) {
    if (i == 5) {
      break;
    }
    sum1 += i;
  }
  if (sum1 != 10) { // 0+1+2+3+4 = 10
    return 6;
  }
  
  // Test 6: Loop continue control
  int sum2 = 0;
  for (i = 0; i < 5; i++) {
    if (i == 2) {
      continue;
    }
    sum2 += i;
  }
  if (sum2 != 8) { // 0+1+3+4 = 8
    return 7;
  }
  
  // Test 7: Nested loops with break and continue
  int sum3 = 0;
  int x;
  int y;
  for (x = 0; x < 3; x++) {
    for (y = 0; y < 3; y++) {
      if (y == 1) {
        continue;
      }
      if (x == 1) {
        break;
      }
      sum3 += (x + y);
    }
  }
  // x=0: y=0 (sum3+=0), y=1 (continue), y=2 (sum3+=2) -> sum3=2
  // x=1: y=0 (break) -> sum3=2
  // x=2: y=0 (sum3+=2), y=1 (continue), y=2 (sum3+=4) -> sum3=8
  if (sum3 != 8) {
    return 8;
  }
  
  return 42;
}
