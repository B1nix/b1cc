/* tests/m20_assign_global_to_local.c - Local aggregate assignment from global aggregate sources for >8-byte structs. */

struct Large {
    long a;
    long b;
};

struct Large global_val = { 111, 222 };

int main(void) {
    struct Large local_val;
    local_val = global_val;
    if (local_val.a != 111) return 1;
    if (local_val.b != 222) return 2;
    return 42;
}
