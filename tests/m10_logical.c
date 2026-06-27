int trigger_side_effect(int *side_effects, int val) {
  side_effects[0] = side_effects[0] + 1;
  return val;
}

int main(void) {
  int side_effects[1] = {0};

  // Test 1: Simple && evaluations
  if (1 && 1) {
    // ok
  } else {
    return 1;
  }
  
  if (1 && 0) {
    return 2;
  }
  
  // Test 2: && short-circuiting
  if (0 && trigger_side_effect(side_effects, 1)) {
    return 3;
  }
  if (side_effects[0] != 0) {
    return 4; // side effect shouldn't have run!
  }
  
  // Test 3: Simple || evaluations
  if (0 || 0) {
    return 5;
  }
  
  if (0 || 1) {
    // ok
  } else {
    return 6;
  }
  
  // Test 4: || short-circuiting
  if (1 || trigger_side_effect(side_effects, 1)) {
    // ok
  } else {
    return 7;
  }
  if (side_effects[0] != 0) {
    return 8; // side effect shouldn't have run!
  }
  
  // Test 5: Nested precedence (|| and &&)
  // 1 || 1 && 0  must parse as 1 || (1 && 0) -> 1 || 0 -> 1.
  // If it parsed as (1 || 1) && 0 -> 1 && 0 -> 0.
  int precedence_test = 1 || trigger_side_effect(side_effects, 1) && 0;
  if (precedence_test) {
    // ok
  } else {
    return 9;
  }
  if (side_effects[0] != 0) {
    return 10; // trigger_side_effect shouldn't have run due to || short-circuiting LHS!
  }
  
  // Now verify that trigger_side_effect does run when RHS is evaluated
  if (0 || trigger_side_effect(side_effects, 1)) {
    // ok
  } else {
    return 11;
  }
  if (side_effects[0] != 1) {
    return 12; // side effect should have run exactly once!
  }
  
  return 42;
}
