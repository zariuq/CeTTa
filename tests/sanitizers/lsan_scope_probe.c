#include <stdlib.h>

static void retain_no_reference(void) {
    unsigned char *allocation = malloc(64u);
    if (!allocation)
        abort();
    *((volatile unsigned char *)allocation) = 1u;
    allocation = NULL;
}

int main(void) {
    retain_no_reference();
    return 0;
}
