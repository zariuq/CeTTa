#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Generate source text, not a pre-lowered document. Each branch contains
 * a group, a plus and an optional; all definitions are reachable from s.
 * The named phases are cumulative public-API runs. The stages mode retains
 * intermediate structured values and brackets each public operation with
 * flushed observation markers, without rendering or re-reading those values. */
static void emit_stages(void) {
    puts("!(let* (($authority (bnf:default-authority \"s\"))\n"
         "         ($_ (println! (EbnfStage begin read)))\n"
         "         ($document (ebench:document))\n"
         "         ($_ (println! (EbnfStage end read)))\n"
         "         ($_ (println! (EbnfStage begin declarations)))\n"
         "         ((BNFDeclarationValidationAcceptedV1 $start $defs $lexicals)\n"
         "          (bnf:validate-ebnf-declarations $document $authority))\n"
         "         ($_ (println! (EbnfStage end declarations)))\n"
         "         ($_ (println! (EbnfStage begin lower)))\n"
         "         ($lowered (bnf:lower-ebnf-document $document $authority))\n"
         "         ((EBNF:LoweredGrammar $original $policy $plain $origins) $lowered)\n"
         "         ($_ (println! (EbnfStage end lower)))\n"
         "         ($_ (println! (EbnfStage begin validate)))\n"
         "         ($validated (bnf:validate-document $plain $policy))\n"
         "         ((BNF:StructuredGrammarAdmitted $checked $checked-policy $validation) $validated)\n"
         "         ($_ (println! (EbnfStage end validate)))\n"
         "         ($_ (println! (EbnfStage begin denote)))\n"
         "         ($denoted (bnf:denote $validated))\n"
         "         ((BNF:StructuredDenotation $dd $da $dv $candidate) $denoted)\n"
         "         ($_ (println! (EbnfStage end denote)))\n"
         "         ($_ (println! (EbnfStage begin load)))\n"
         "         ($parser (bnf:load-parser $denoted))\n"
         "         ((NativeHandle $kind $identifier) $parser)\n"
         "         ($_ (println! (EbnfStage end load))))\n"
         "   (let $answers (collapse\n"
         "     (let* (($_ (println! (EbnfStage begin parse)))\n"
         "            ((LangDef:AuthoredParseAccepted $name $binding $source $trees)\n"
         "             (langdef:parse-authored-text $parser \"a0!\"))\n"
         "            ($_ (println! (EbnfStage end parse)))\n"
         "            ($_ (println! (EbnfStage begin project)))\n"
         "            ((EBNF:DerivationsCons $tree (EBNF:DerivationsNil))\n"
         "             (bnf:project-ebnf-trees $lowered $trees))\n"
         "            ($_ (println! (EbnfStage end project))))\n"
         "       (EbnfBenchStages (ebench:entries (ebench:plain-entries $plain))\n"
         "                        (ebench:origins $origins))))\n"
         "     (let $_ (langdef:close-authored-parser $parser) (superpose $answers))))");
}

static void emit_validation_stages(void) {
    /* Same installed checked declaration entry and analysis entry as
     * bnf-typed-discovery:checked-document; no replacement validator. */
    puts("!(case (ebench:lower)\n"
         "   (((EBNF:LoweredGrammar $original $authority $plain $origins)\n"
         "      (let $language (langdef:load-language \"langdef/bnf/plain_bnf_grammar_v1.metta\")\n"
         "        (let $answers (collapse\n"
         "          (let* (($input (bnf-v1:grammar-input $plain $authority))\n"
         "                 ($_ (println! (EbnfStage begin shape)))\n"
         "                 ((LangDef:TermAdmitted) (langdef:admit-term $language \"BnfGrammarInput\" $input))\n"
         "                 ($_ (println! (EbnfStage end shape)))\n"
         "                 ($_ (println! (EbnfStage begin checked-declarations)))\n"
         "                 ((gslt:result:BNFValidateGrammarDiscoveryV1:10 $declarations)\n"
         "                  (gslt:admitted-entry:BNFValidateGrammarDiscoveryV1:10 $language $input))\n"
         "                 ((BNFDeclarationValidationAcceptedV1 $start $definitions $lexicals) $declarations)\n"
         "                 ($_ (println! (EbnfStage end checked-declarations)))\n"
         "                 ($_ (println! (EbnfStage begin analysis)))\n"
         "                 ((BNFSemanticValidationAcceptedV1 $s $d $l $a)\n"
         "                  (bnf:analyze-declaration-valid-document $start $definitions $lexicals))\n"
         "                 ($_ (println! (EbnfStage end analysis))))\n"
         "            EbnfBenchValidated))\n"
         "          (let $_ (langdef:close-language $language) (superpose $answers)))))\n"
         "    ($other (EbnfBenchUnexpected $other))))");
}

int main(int argc, char **argv) {
    char *end = NULL;
    unsigned long count;
    const char *phase;
    if (argc != 3) {
        fprintf(stderr, "usage: %s BRANCH_COUNT read|lower|declarations|validate|load|parse|project|stages|validation-stages|analysis-stages\n", argv[0]);
        return 2;
    }
    errno = 0;
    count = strtoul(argv[1], &end, 10);
    if (errno || !argv[1][0] || argv[1][0] == '-' || *end || !count || count > UINT32_MAX / 4) {
        fputs("BRANCH_COUNT must be positive and fit the rule-count arithmetic\n", stderr);
        return 2;
    }
    phase = argv[2];
    if (strcmp(phase, "read") && strcmp(phase, "lower") && strcmp(phase, "declarations") &&
        strcmp(phase, "validate") && strcmp(phase, "load") && strcmp(phase, "parse") &&
        strcmp(phase, "project") && strcmp(phase, "stages") &&
        strcmp(phase, "validation-stages") && strcmp(phase, "analysis-stages")) {
        fputs("unknown benchmark phase\n", stderr);
        return 2;
    }
    fputs("(= (ebench:source) \"<s> ::= ", stdout);
    for (unsigned long i = 0; i < count; ++i)
        printf("%s<r%lu>", i ? " | " : "", i);
    fputs("\\n", stdout);
    for (unsigned long i = 0; i < count; ++i)
        printf("<r%lu> ::= (\\\"a%lu\\\" | \\\"b%lu\\\")+ \\\"!\\\"?\\n", i, i, i);
    puts("\")\n"
         "(= (ebench:document) (bnf:document (bnf:read-ebnf-text (ebench:source))))\n"
         "(= (ebench:lower) (bnf:lower-ebnf-document (ebench:document) (bnf:default-authority \"s\")))\n"
         "(= (ebench:entries (bnf-v1:entries-nil)) 0)\n"
         "(= (ebench:entries (bnf-v1:entries-cons $head $tail)) (+ 1 (ebench:entries $tail)))\n"
         "(= (ebench:origins (ebnf-v1:origins-nil)) 0)\n"
         "(= (ebench:origins (ebnf-v1:origins-cons $head $tail)) (+ 1 (ebench:origins $tail)))");
    if (!strcmp(phase, "analysis-stages")) {
        puts("!(ebench:run-graph-components (ebench:lower))");
    } else if (!strcmp(phase, "validation-stages")) {
        emit_validation_stages();
    } else if (!strcmp(phase, "stages")) {
        puts("(= (ebench:plain-entries (bnf-v1:document $entries $span)) $entries)");
        emit_stages();
    } else if (!strcmp(phase, "read")) {
        puts("!(case (ebench:document)\n"
             "   (((bnf-v1:document $entries $span) (EbnfBenchRead (ebench:entries $entries)))\n"
             "    ($other (EbnfBenchUnexpected $other))))");
    } else if (!strcmp(phase, "lower")) {
        puts("!(case (ebench:lower)\n"
             "   (((EBNF:LoweredGrammar $original $authority (bnf-v1:document $entries $span) $origins)\n"
             "      (EbnfBenchLower (ebench:entries $entries) (ebench:origins $origins)))\n"
             "    ($other (EbnfBenchUnexpected $other))))");
    } else if (!strcmp(phase, "declarations")) {
        puts("!(case (bnf:validate-ebnf-declarations (ebench:document) (bnf:default-authority \"s\"))\n"
             "   (((BNFDeclarationValidationAcceptedV1 $start $definitions $lexicals) EbnfBenchDeclarations)\n"
             "    ($other (EbnfBenchUnexpected $other))))");
    } else if (!strcmp(phase, "validate")) {
        puts("!(case (ebench:lower)\n"
             "   (((EBNF:LoweredGrammar $original $authority $plain $origins)\n"
             "      (case (bnf:validate-document $plain $authority)\n"
             "        (((BNF:StructuredGrammarAdmitted $checked $policy $validation) EbnfBenchValidated)\n"
             "         ($other (EbnfBenchUnexpected $other)))))\n"
             "    ($other (EbnfBenchUnexpected $other))))");
    } else {
        puts("!(case (bnf:parser-from-ebnf-text (ebench:source) \"s\")\n"
             "   (((EBNF:SourceParser $handle $gb $gs $receipt)\n"
             "      (let $answers (collapse");
        if (!strcmp(phase, "load")) {
            puts("        EbnfBenchLoaded)");
        } else if (!strcmp(phase, "parse")) {
            puts("        (case (langdef:parse-authored-text $handle \"a0!\")\n"
                 "          (((LangDef:AuthoredParseAccepted $name $b $s $trees)\n"
                 "             (EbnfBenchParsed (size-atom $trees)))\n"
                 "           ($other (EbnfBenchUnexpected $other)))))");
        } else {
            puts("        (case (bnf:parse-ebnf-text (EBNF:Parser $handle $receipt) \"a0!\")\n"
                 "          (((EBNF:Parse (EBNF:Accepted $b $s\n"
                 "               (EBNF:DerivationsCons $tree (EBNF:DerivationsNil))) $r)\n"
                 "             EbnfBenchProjected)\n"
                 "           ($other (EbnfBenchUnexpected $other)))))");
        }
        puts("        (let $_ (langdef:close-authored-parser $handle) (superpose $answers))))\n"
             "    ($other (EbnfBenchUnexpected $other))))");
    }
    return ferror(stdout) ? 1 : 0;
}
