/* choice_values: the compiled choice.module's judgment c:toss on c:coin, every value it gives (rc_solve),
 * one constructor name per line.
 *   cc -I. -DCHOICE_C='"choice.c"' tests/tcalc/modules/choice_values.c -o choice_values */
#include "native/rule_runtime/rc_rt.c"
#include CHOICE_C
#include <stdio.h>
#include <string.h>

static int named(const char *name) {
    for (int c = 0; c < rc_nctors; c++) if (!strcmp(rc_ctor_name[c], name)) return c;
    return -1;
}
static void say(const RcK *k, T *answer) { (void)k; puts(rc_ctor_name[answer->tag]); }

int main(void) {
    rc_runtime_init(0);
    rc_components_init();
    int toss = named("c:toss"), coin = named("c:coin");
    if (toss < 0 || coin < 0) return 2;
    T *args[1] = {rc_construct(coin, NULL)};
    RcK k = {say};
    rc_solve(toss, args, &k);
    return 0;
}
