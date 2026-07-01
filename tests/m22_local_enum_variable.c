enum task_state {
    TASK_READY = 40,
    TASK_SLEEPING = 42,
};

int main(void) {
    enum task_state expected = TASK_SLEEPING;
    return expected;
}
