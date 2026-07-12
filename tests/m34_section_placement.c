__attribute__((section("__TEXT,__m34_placement"))) int placed(void) { return 42; }
int main(void) { return placed(); }
