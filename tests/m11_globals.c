int g_zero;
int g_val = 42;
int g_arr[3] = {10, 20, 30};
int g_arr_pad[5] = {1, 2};
int g_arr_zero[4];

int main(void) {
  // Test 1: Constant-initialized global
  if (g_val != 42) {
    return 1;
  }
  
  // Test 2: Zero-initialized global
  if (g_zero != 0) {
    return 2;
  }
  
  // Test 3: Modify global
  g_val = 100;
  if (g_val != 100) {
    return 3;
  }
  
  // Test 4: Initialized global array
  if (g_arr[0] != 10 || g_arr[1] != 20 || g_arr[2] != 30) {
    return 4;
  }
  
  // Test 5: Partially initialized global array
  if (g_arr_pad[0] != 1 || g_arr_pad[1] != 2 || g_arr_pad[2] != 0 || g_arr_pad[3] != 0 || g_arr_pad[4] != 0) {
    return 5;
  }
  
  // Test 6: Uninitialized global array
  if (g_arr_zero[0] != 0 || g_arr_zero[1] != 0 || g_arr_zero[2] != 0 || g_arr_zero[3] != 0) {
    return 6;
  }
  
  // Test 7: Write to global array
  g_arr[1] = 200;
  if (g_arr[1] != 200) {
    return 7;
  }
  
  return 42;
}
