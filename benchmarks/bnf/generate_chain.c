#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Emit a public-API workload, not a preconstructed grammar document.
 * Each phase includes all preceding stages, including reading the source.
 * Load lib_bnf and its generated admission program before this output. */
int main(int argc, char **argv) {
    char *end = NULL;
    unsigned long long count;
    const char *phase;
    int indexed = 0;
    int declaration_map = 0;
    int reverse = 0;
    int graph_indexed = 0;
    int graph_worklist = 0;

    if (argc < 3 || argc > 6) {
        fprintf(stderr, "usage: %s RULE_COUNT read|declarations|validate [linear|indexed|mapped] [forward|reverse] [linear|indexed|worklist]\n", argv[0]);
        return 2;
    }
    errno = 0;
    count = strtoull(argv[1], &end, 10);
    if (errno || !argv[1][0] || argv[1][0] == '-' || *end ||
        !count || count > UINT32_MAX) {
        fputs("RULE_COUNT must be a positive 32-bit integer\n", stderr);
        return 2;
    }
    phase = argv[2];
    if (strcmp(phase, "read") && strcmp(phase, "declarations") &&
        strcmp(phase, "validate")) {
        fputs("unknown benchmark phase\n", stderr);
        return 2;
    }
    if (argc >= 4) {
        if (!strcmp(argv[3], "indexed")) indexed = 1;
        else if (!strcmp(argv[3], "mapped")) {
            indexed = 1;
            declaration_map = 1;
        }
        else if (strcmp(argv[3], "linear")) {
            fputs("unknown collector route\n", stderr);
            return 2;
        }
    }
    if (argc >= 5) {
        if (!strcmp(argv[4], "reverse")) reverse = 1;
        else if (strcmp(argv[4], "forward")) {
            fputs("unknown declaration order\n", stderr);
            return 2;
        }
    }
    if (argc == 6) {
        if (!strcmp(argv[5], "indexed")) graph_indexed = 1;
        else if (!strcmp(argv[5], "worklist")) {
            graph_indexed = 1;
            graph_worklist = 1;
        }
        else if (strcmp(argv[5], "linear")) {
            fputs("unknown graph membership route\n", stderr);
            return 2;
        }
    }

    puts("; Generated chain workload: r0 -> r1 -> ... -> literal a.");
    puts("(= (bench:authority)\n"
         "   (bnf:grammar-authority\n"
         "     (bnf-v1:text-cons 114\n"
         "       (bnf-v1:text-cons 48 (bnf-v1:text-nil)))\n"
         "     (bnf:empty-lexical-environment)))");
    fputs("(= (bench:read) (bnf:read-text \"", stdout);
    for (uint32_t position = 0; position < (uint32_t)count; ++position) {
        uint32_t i = reverse ? (uint32_t)count - 1u - position : position;
        if ((unsigned long long)i + 1u < count)
            printf("<r%u> ::= <r%u>\\n", i, i + 1u);
        else
            printf("<r%u> ::= \\\"a\\\"\\n", i);
    }
    puts("\"))");
    puts("(= (bench:entry-count (bnf-v1:entries-nil)) 0)\n"
         "(= (bench:entry-count (bnf-v1:entries-cons $head $tail))\n"
         "   (+ 1 (bench:entry-count $tail)))\n"
         "(= (bench:definition-count BNFDefinitionsNilV1) 0)\n"
         "(= (bench:definition-count (BNFDefinitionsConsV1 $head $tail))\n"
         "   (+ 1 (bench:definition-count $tail)))");
    if (graph_indexed) {
        const char *entry = graph_worklist ? "BNFAnalyzeDocumentWorklistV1"
                                         : "BNFAnalyzeDocumentIndexedV1";
        printf("(= (bench:indexed-graph $start $definitions $lexicals)\n"
               "   (let (gslt:result:%s:1110 $result)\n"
               "        (gslt:entry:%s:1110\n"
               "          $start $definitions $lexicals) $result))\n", entry, entry);
    }
    if (indexed || graph_indexed) {
        const char *entry = declaration_map ? "BNFValidateGrammarDiscoveryV1"
                                           : "BNFValidateGrammarIndexedV1";
        printf("; Candidate route; not a replacement of public library definitions.\n"
               "(= (bench:indexed-declarations $input)\n"
               "   (let (gslt:result:%s:10 $result)\n"
               "        (gslt:entry:%s:10 $input) $result))\n", entry, entry);
        puts("(= (bench:indexed-validate\n"
             "      (BNF:ReadValue (BNF:GrammarDocument $binding $source $document)) $authority)\n"
             "   (case (bnf:admit-grammar-input (bnf-v1:grammar-input $document $authority))\n"
             "     (((BNF:GrammarInputAdmitted $input)\n"
             "        (case");
        puts(indexed ? "          (bench:indexed-declarations $input)"
                     : "          (bnf:validate-declarations $input)");
        puts(
             "          (((BNFDeclarationValidationAcceptedV1 $start $definitions $lexicals)\n"
             "             (BNF:SemanticGrammarAdmitted $binding $source $document $authority");
        puts(graph_indexed
             ? "               (bench:indexed-graph $start $definitions $lexicals)))"
             : "               (bnf:analyze-declaration-valid-document $start $definitions $lexicals)))");
        puts(
             "           ($refusal (BnfBenchDeclarationRefused $refusal)))))\n"
             "      ($refusal (BnfBenchInputRefused $refusal)))))");
    }
    if (!strcmp(phase, "read")) {
        puts("(= (bench:observe\n"
             "      (BNF:ReadValue\n"
             "        (BNF:GrammarDocument $binding $source\n"
             "          (bnf-v1:document $entries $span))))\n"
             "   (BnfBenchAccepted (bench:entry-count $entries)))\n"
             "!(println! (bench:observe (bench:read)))");
    } else if (!strcmp(phase, "declarations")) {
        puts("(= (bench:observe\n"
             "      (BNFDeclarationValidationAcceptedV1\n"
             "        $start $definitions $lexicals))\n"
             "   (BnfBenchAccepted (bench:definition-count $definitions)))\n"
             "(= (bench:run\n"
             "      (BNF:ReadValue\n"
             "        (BNF:GrammarDocument $binding $source $document)))\n"
             "   (bench:observe");
        puts(indexed ? "     (bench:indexed-declarations" : "     (bnf:validate-declarations");
        puts(
             "       (bnf-v1:grammar-input $document (bench:authority)))))\n"
             "!(println! (bench:run (bench:read)))");
    } else {
        puts("(= (bench:observe\n"
             "      (BNF:SemanticGrammarAdmitted\n"
             "        $binding $source $document $authority\n"
             "        (BNFSemanticValidationAcceptedV1\n"
             "          $start $definitions $lexicals $analysis)))\n"
             "   (BnfBenchAccepted (bench:definition-count $definitions)))\n"
             "!(println!");
        puts((indexed || graph_indexed)
             ? "   (bench:observe (bench:indexed-validate (bench:read) (bench:authority))))"
             : "   (bench:observe (bnf:validate (bench:read) (bench:authority))))");
    }
    return ferror(stdout) ? 1 : 0;
}
