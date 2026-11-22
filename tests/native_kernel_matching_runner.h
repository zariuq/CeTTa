#include "native/kernel/kernel.h"
#include <stdlib.h>

int main(int argc, char **argv) {
    CettaKernelMode kernel_mode;
    char kernel_error[256] = {0};
    if (argc != 2 || !cetta_kernel_mode(argv[1], &kernel_mode) ||
        !cetta_kernel_select(kernel_mode, kernel_error, sizeof kernel_error)) {
        fprintf(stderr, "kernel test selection failed: %s\n", kernel_error);
        return 2;
    }
    cetta_kernel_enable_stats();
#ifdef KERNEL_CASE_ARGV
    char *case_argv[] = {argv[0], NULL};
    int result = kernel_matching_case_main(1, case_argv);
#else
    int result = kernel_matching_case_main();
#endif
    CettaKernelStats kernel_stats = cetta_kernel_stats();
    bool strict = kernel_mode == CETTA_KERNEL_X86;
    if (strict && (kernel_stats.c_calls ||
        !kernel_stats.x86_calls)) {
        fprintf(stderr, "native matching sentinel: C=%llu x86=%llu\n",
            (unsigned long long)kernel_stats.c_calls,
            (unsigned long long)kernel_stats.x86_calls);
        result = 1;
    }
    cetta_kernel_print_stats();
    cetta_kernel_shutdown();
    return result;
}
