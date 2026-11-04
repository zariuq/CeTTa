/* Link GMP statically with --wrap=malloc,--wrap=realloc. With "refuse",
 * exercise its default fatal allocation boundary in a separate process. */
#include <errno.h>
#include <gmp.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool refusing;
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
void *__wrap_malloc(size_t size) {
    if (refusing) { errno = ENOMEM; return NULL; }
    return __real_malloc(size);
}
void *__wrap_realloc(void *pointer, size_t size) {
    if (refusing) { errno = ENOMEM; return NULL; }
    return __real_realloc(pointer, size);
}
int main(int argc, char **argv) {
    refusing = argc > 1 && !strcmp(argv[1], "refuse");
    mpz_t value;
    mpz_init2(value, 1024u);
    mpz_set_ui(value, 42u);
    int status = mpz_cmp_ui(value, 42u) != 0;
    mpz_clear(value);
    return status;
}
