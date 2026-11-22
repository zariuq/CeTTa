/* A standalone driver for the tcalc lane's tests: the core, the reader and
 * the runner without the rest of CeTTa.
 *   tcalc_main [PROFILE] FILE              run a program
 *   tcalc_main --plans FILE                write its rules' join plans
 *   tcalc_main --translate FROM TO FILE    write it in another notation */
#include "tcalc_cli.h"

#include <string.h>

int main(int argc, char **argv) {
    if (argc == 5 && strcmp(argv[1], "--translate") == 0)
        return cetta_tcalc_translate_file(argv[4], argv[2], argv[3]);
    if (argc == 3 && strcmp(argv[1], "--plans") == 0) return cetta_tcalc_plans_file(argv[2], NULL);
    if (argc < 2 || argc > 3) return 2;
    return cetta_tcalc_run_file(argv[argc - 1], argc == 3 ? argv[1] : NULL, NULL, NULL);
}
