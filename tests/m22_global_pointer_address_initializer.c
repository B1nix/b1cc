int target_value;
int *target_ptr = &target_value;

int main(void) {
    target_value = 42;
    return *target_ptr;
}
