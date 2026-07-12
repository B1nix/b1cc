/* M34: hosted profile has the target runtime boundary and advertises itself. */
#include <stdio.h>
#include <stdlib.h>

#if __STDC_HOSTED__ != 1
#error hosted profile did not define __STDC_HOSTED__ to one
#endif

int main(void) {
    FILE *f = fopen("/dev/null", "w");
    if (!f) return 1;
    fputs("hosted", f);
    return fclose(f) == 0 ? 0 : 2;
}
