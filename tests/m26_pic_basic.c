int global_var = 42;
int *global_ptr;

int main(void) {
    global_var = 10;
    global_ptr = &global_var;
    return global_var;
}
