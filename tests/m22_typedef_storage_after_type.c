typedef struct {
    int init;
    int value;
} TCCSem;

#define TCC_SEM(s) TCCSem s

TCC_SEM(static tcc_compile_sem);

int main(void) {
    tcc_compile_sem.value = 42;
    return tcc_compile_sem.value;
}
