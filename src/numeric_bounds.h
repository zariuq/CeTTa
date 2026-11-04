#ifndef CETTA_NUMERIC_BOUNDS_H
#define CETTA_NUMERIC_BOUNDS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <fenv.h>
#include <float.h>
#include <math.h>

/* Probe inside a saved floating environment: subnormal results and inputs
 * must both survive (FTZ/DAZ), and a narrow input widens without loss. */
static inline bool cetta_numeric_rounding_admitted(bool narrow) {
#if FLT_EVAL_METHOD != 0 || defined(__FAST_MATH__) || FLT_RADIX != 2 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
    (void)narrow;
    return false;
#else
    if (sizeof(double) != sizeof(uint64_t) || fegetround() != FE_TONEAREST) return false;
    volatile double wide = DBL_MIN, half = .5, twice = 2.;
    volatile double small = wide * half;
    if (small * twice != DBL_MIN) return false;
    if (narrow) {
        if (sizeof(float) != 4u || FLT_MANT_DIG != 24 || FLT_MAX_EXP != 128) return false;
        volatile float low = FLT_MIN, half32 = .5f, twice32 = 2.f;
        volatile float small32 = low * half32;
        if (small32 * twice32 != FLT_MIN) return false;
    }
    return true;
#endif
}

static inline bool cetta_numeric_current_rounding_admitted(bool narrow) {
    fenv_t saved;
    if (feholdexcept(&saved) != 0) return false;
    bool admitted = cetta_numeric_rounding_admitted(narrow);
    return fesetenv(&saved) == 0 && admitted;
}

/* Adjacent outward steps of finite computed results. Nonfinite values pass
 * through for the caller's range refusal. This is not a public nextafter API. */
static inline double cetta_numeric_up(double value) {
    if (!isfinite(value)) return value;
    if (value == 0) return DBL_TRUE_MIN;
    uint64_t bits; memcpy(&bits, &value, sizeof(bits));
    if (value > 0) bits++; else bits--;
    memcpy(&value, &bits, sizeof(value)); return value;
}
static inline double cetta_numeric_down(double value) {
    if (!isfinite(value)) return value;
    if (value == 0) return -DBL_TRUE_MIN;
    uint64_t bits; memcpy(&bits, &value, sizeof(bits));
    if (value > 0) bits--; else bits++;
    memcpy(&value, &bits, sizeof(value)); return value;
}
static inline double cetta_numeric_add_up(double a, double b) { return cetta_numeric_up(a + b); }
static inline double cetta_numeric_mul_up(double a, double b) { return cetta_numeric_up(a * b); }

#endif
