#include "native/decimal_value_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int passed;
static int failed;

static void rational(const char *text, const char *numerator, const char *denominator) {
    char *n = NULL;
    char *d = NULL;
    bool ok = cetta_decimal_rational_value_v1(text, &n, &d);
    if (!numerator) {
        if (ok) {
            fprintf(stderr, "rational %s: accepted %s/%s, expected rejection\n", text, n, d);
            failed++;
        } else {
            passed++;
        }
    } else if (!ok || strcmp(n, numerator) != 0 || strcmp(d, denominator) != 0) {
        fprintf(stderr, "rational %s: got %s/%s, expected %s/%s\n", text,
                ok ? n : "(rejected)", ok ? d : "", numerator, denominator);
        failed++;
    } else {
        passed++;
    }
    free(n);
    free(d);
}

static void real(const char *text, const char *expected) {
    char *out = NULL;
    bool ok = cetta_decimal_real_canonical_v1(text, &out);
    if (!expected) {
        if (ok) {
            fprintf(stderr, "real %s: accepted %s, expected rejection\n", text, out);
            failed++;
        } else {
            passed++;
        }
    } else if (!ok || strcmp(out, expected) != 0) {
        fprintf(stderr, "real %s: got %s, expected %s\n", text, ok ? out : "(rejected)", expected);
        failed++;
    } else {
        passed++;
    }
    free(out);
}

static void real_double(const char *text, bool ok_expected, double expected) {
    double value = -1.0;
    bool ok = cetta_decimal_real_double_v1(text, &value);
    if (ok != ok_expected || (ok && (value != expected || signbit(value) != signbit(expected)))) {
        fprintf(stderr, "double %s: got %s %.17g, expected %s %.17g\n", text,
                ok ? "ok" : "none", value, ok_expected ? "ok" : "none", expected);
        failed++;
    } else {
        passed++;
    }
}

int main(void) {
    rational("1/2", "1", "2");
    rational("2/4", "1", "2");
    rational("+1/2", "1", "2");
    rational("-6/4", "-3", "2");
    rational("-0/5", "0", "1");
    rational("0/7", "0", "1");
    rational("12/1", "12", "1");
    rational("1/3", "1", "3");
    rational("4294967296/8589934592", "1", "2");
    rational("123456789012345678901234567890/987654321098765432109876543210",
             "13717421", "109739369");
    rational("-340282366920938463463374607431768211456/18446744073709551616",
             "-18446744073709551616", "1");
    rational("1/0", NULL, NULL);
    rational("1/", NULL, NULL);
    rational("/2", NULL, NULL);
    rational("1.5/2", NULL, NULL);
    rational("12", NULL, NULL);

    real("1.02", "1.02");
    real("1.020", "1.02");
    real("+1.02", "1.02");
    real("-2.5", "-2.5");
    real("100.0", "100.0");
    real("0.0", "0.0");
    real("-0.0", "0.0");
    real("0E5", "0.0");
    real("0.5", "0.5");
    real("0.0015", "0.0015");
    real("0.000001", "0.000001");
    real("0.0000001", "1E-7");
    real("1E20", "100000000000000000000.0");
    real("1E21", "1E21");
    real("15.0", "15.0");
    real("1.5E1", "15.0");
    real("150E-1", "15.0");
    real("007E2", "700.0");
    real("123.456e78", "1.23456E80");
    real("-123.456E-78", "-1.23456E-76");
    real("1E+05", "100000.0");
    real("2.50e-3", "0.0025");
    real("1E999999999999999999999", "1E999999999999999999999");
    real("12.5E-99999999999999999999", "1.25E-99999999999999999998");
    real("-10.0E99999999999999999999", "-1E100000000000000000000");
    real("1", NULL);
    real("1.", NULL);
    real(".5", NULL);
    real("1E", NULL);
    real("1.5x", NULL);
    real("1/2", NULL);

    real_double("1.02", true, 1.02);
    real_double("-2.5", true, -2.5);
    real_double("0.30000000000000004", true, 0.30000000000000004);
    real_double("1.5E1", true, 15.0);
    real_double("-0.0", true, 0.0);
    real_double("0E999", true, 0.0);
    real_double("4.9406564584124654E-324", true, 4.9406564584124654E-324);
    real_double("1E-400", false, 0.0);
    real_double("1E400", false, 0.0);
    real_double("-1.8E308", false, 0.0);
    real_double("1.7976931348623157E308", true, 1.7976931348623157E308);
    real_double("1", false, 0.0);
    {
        static const struct { double value; const char *text; } cases[] = {
            {1.02, "1.02"}, {0.1, "0.1"}, {0.30000000000000004, "0.30000000000000004"},
            {100.0, "100.0"}, {-2.5, "-2.5"}, {0.0, "0.0"}, {-0.0, "0.0"}, {1e-7, "1E-7"},
            {1.23456e80, "1.23456E80"}, {5e-324, "5E-324"}, {1.7976931348623157e308, "1.7976931348623157E308"},
            {1e21, "1E21"}, {1e20, "100000000000000000000.0"}, {15.0, "15.0"}};
        for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char *text = NULL;
            bool ok = cetta_decimal_real_from_double_v1(cases[i].value, &text);
            if (!ok || strcmp(text, cases[i].text) != 0) {
                fprintf(stderr, "from double %.17g: got %s, expected %s\n", cases[i].value,
                        ok ? text : "(none)", cases[i].text);
                failed++;
            } else {
                passed++;
            }
            free(text);
        }
        {
            char *text = NULL;
            if (cetta_decimal_real_from_double_v1(1.0 / 0.0, &text)) {
                fprintf(stderr, "from double inf: accepted %s\n", text);
                failed++;
            } else {
                passed++;
            }
            free(text);
        }
    }
    printf("(DecimalValueV1Summary %d %d)\n", passed, failed);
    return failed ? 1 : 0;
}
