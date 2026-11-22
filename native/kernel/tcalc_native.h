#ifndef CETTA_KERNEL_TCALC_NATIVE_H
#define CETTA_KERNEL_TCALC_NATIVE_H
#include <stdbool.h>
#include <stddef.h>

/* Whether tcalc's native unifier and term builder load and match their
 * programs (tcalc_native.c); if not, why, in error. */
bool cetta_kernel_tcalc_native_ready(char *error,size_t size);
#if defined(__GNUC__)
extern bool cetta_kernel_tcalc_native_ready(char *error,size_t size) __attribute__((weak));
#endif
#endif
