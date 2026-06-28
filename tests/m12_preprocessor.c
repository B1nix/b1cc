#define LOCAL_VALUE 2
#include <m12_value.h>

#if defined(M12_VALUE) && !defined(MISSING_VALUE)
#define SELECTED 1
#else
#define SELECTED 0
#endif

#ifdef LOCAL_VALUE
#define LOCAL_SELECTED 1
#else
#define LOCAL_SELECTED 0
#endif

int main(void) {
  if (SELECTED != 1) return 1;
  if (LOCAL_SELECTED != 1) return 2;
  return M12_ADD(M12_VALUE, LOCAL_VALUE);
}
