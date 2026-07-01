#define M21_ITEM(name, value) int generated_##name(void) { return value; }
M21_ITEM(alpha, 21)
#undef M21_ITEM

