/* tests/m22_struct_function_pointer_return_pointer_field.c - function pointer field returning pointer. */

struct Node {
    int value;
};

struct Ops {
    const char *name;
    struct Node *(*mount)(const char *source);
};

static struct Node node;

static struct Node *mount_cb(const char *source) {
    (void)source;
    node.value = 42;
    return &node;
}

static struct Ops ops = {
    .name = "ops",
    .mount = mount_cb,
};

int main(void) {
    return ops.mount("disk")->value;
}
