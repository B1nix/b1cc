/* tests/m22_typedef_array_alias.c - System-header style typedef array aliases. */

typedef int IntTriple[3];
typedef long LongPair[2], *LongPairPtr;

int main(void) {
    if (sizeof(IntTriple) != 12) return 1;
    if (sizeof(LongPair) != sizeof(long) * 2) return 2;
    if (sizeof(LongPairPtr) != sizeof(void *)) return 3;
    return 42;
}
