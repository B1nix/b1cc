/* M34: a weak-attributed definition links and is callable when not overridden. */
int __attribute__((weak)) answer(void) { return 42; }
int main(void) { return answer(); }
