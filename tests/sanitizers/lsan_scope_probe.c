#include <stdlib.h>
#include <string.h>

#if CETTA_LSAN_PROLOG_SCOPE_PROBE
#include <SWI-Prolog.h>
#endif

static void retain_no_reference(void) {
    unsigned char *allocation = malloc(64u);
    if (!allocation)
        abort();
    *((volatile unsigned char *)allocation) = 1u;
    allocation = NULL;
}

#if CETTA_LSAN_PROLOG_SCOPE_PROBE
static foreign_t leak_from_prolog(void) {
    retain_no_reference();
    return TRUE;
}

static int probe_foreign_call(void) {
    char program[] = "lsan-scope-probe";
    char quiet[] = "-q";
    char no_signals[] = "--no-signals";
    char no_alert_signal[] = "--sigalert=0";
    char no_init[] = "-f";
    char no_init_file[] = "none";
    char *arguments[] = {
        program, quiet, no_signals, no_alert_signal,
        no_init, no_init_file, NULL,
    };
    if (!PL_initialise(6, arguments))
        abort();
    if (!PL_register_foreign("lsan_native_scope_leak", 0,
                             leak_from_prolog, 0))
        abort();
    predicate_t predicate = PL_predicate("lsan_native_scope_leak", 0, NULL);
    if (!PL_call_predicate(NULL, PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
                           predicate, 0))
        abort();
    (void)PL_cleanup(PL_CLEANUP_NO_CANCEL | PL_CLEANUP_NO_RECLAIM_MEMORY);
    return 0;
}
#endif

int main(int argc, char **argv) {
#if CETTA_LSAN_PROLOG_SCOPE_PROBE
    if (argc == 2 && strcmp(argv[1], "--foreign") == 0)
        return probe_foreign_call();
#else
    (void)argv;
#endif
    if (argc != 1)
        return 2;
    retain_no_reference();
    return 0;
}
