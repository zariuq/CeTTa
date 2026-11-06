/*
 * mg-sexprinfo [-kernel] [-hf|-mizar|-hoas|-setmm] [-owned FILE] [-I SIGNATURE]... FILE
 *
 * Reads and evaluates a Megalodon document natively and prints what
 * `megalodon -sexprinfo` prints: the -sexprinfo rows, then Megalodon's
 * syntax error or failure line.  The exit status is Megalodon's: 0, 1 on a
 * syntax error or failure, 2 where Megalodon stops with an uncaught
 * exception.  Proof tactics beyond structure, admit, aby and the closing
 * commands are counted as unchecked (stderr), not evaluated.
 *
 * -kernel also admits every declaration into the native kernel as it is
 * evaluated and prints the kernel's report on stderr; a kernel refusal of
 * what Megalodon accepts exits 3.  -owned names Megalodon's ownership
 * file, which changes nothing checked here (its checks are disabled
 * upstream).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mg_elab.h"
#include "mg_run.h"

int main(int argc, char **argv) {
    MgElabOptions options = {.theory = MG_PROFILE_EGAL, .rows = stdout};
    MgRunFile files[256];
    int nfiles = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-hf") == 0) options.theory = MG_PROFILE_HF;
        else if (strcmp(argv[i], "-mizar") == 0) options.theory = MG_PROFILE_MIZAR;
        else if (strcmp(argv[i], "-hoas") == 0) options.theory = MG_PROFILE_HOAS;
        else if (strcmp(argv[i], "-setmm") == 0) options.theory = MG_PROFILE_SETMM;
        else if (strcmp(argv[i], "-kernel") == 0) options.kernel = true;
        else if (strcmp(argv[i], "-owned") == 0 && i + 1 < argc) ++i;
        else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc && nfiles < 255)
            files[nfiles++] = (MgRunFile){argv[++i], true};
        else if (argv[i][0] != '-' && nfiles < 255)
            files[nfiles++] = (MgRunFile){argv[i], false};
        else {
            fprintf(stderr, "usage: mg-sexprinfo [-hf|-mizar|-hoas|-setmm] [-I SIG]... FILE\n");
            return 2;
        }
    }
    if (!nfiles || files[nfiles - 1].signature) {
        fprintf(stderr, "usage: mg-sexprinfo [-hf|-mizar|-hoas|-setmm] [-I SIG]... FILE\n");
        return 2;
    }
    MgElab *e = mg_elab_new(&options);
    if (!e) {
        fprintf(stderr, "mg-sexprinfo: cannot start the evaluator\n");
        return 2;
    }
    char message[4096];
    MgRunStatus run = mg_run_files(e, files, nfiles, message, sizeof message);
    int status = 0;
    switch (run) {
    case MG_RUN_OK: break;
    case MG_RUN_FAILURE: printf("%s\n", message); status = 1; break;
    case MG_RUN_EXCEPTION: fprintf(stderr, "%s\n", message); status = 2; break;
    case MG_RUN_REFUSED: fprintf(stderr, "%s\n", message); status = 3; break;
    case MG_RUN_UNREADABLE: fprintf(stderr, "mg-sexprinfo: %s\n", message); status = 2; break;
    }
    fflush(stdout);
    char summary[512];
    if (options.kernel && mg_elab_finish(e, summary, sizeof summary) != MG_OK && status == 0)
        status = 3;
    if (options.kernel)
        fprintf(stderr, "%s\n", summary);
    if (mg_elab_unchecked_tactics(e))
        fprintf(stderr, "mg-sexprinfo: %llu proof tactics unchecked\n",
                (unsigned long long)mg_elab_unchecked_tactics(e));
    mg_elab_free(e);
    return status;
}
