!(import! &self ../../../lib/lib_bnf.metta)

; Recognition succeeds even when its derivations cannot be enumerated as
; a finite tuple. Evidence requests check GLL/GLR packed-forest agreement.
(= (ebnf-cycle-test:run $grammar $word)
   (let $parser (bnf:parser-from-ebnf-text $grammar "s")
     (case $parser
       (((EBNF:SourceParser $handle $gb $gs $receipt)
          (let $plain (bnf:parse-ebnf-text $parser $word)
            (let $evidence (bnf:parse-ebnf-text-evidence $parser $word 10000)
              (let $_ (bnf:close-ebnf-parser $parser)
                (case ($plain $evidence)
                  ((((EBNF:SourceParse (EBNF:Cyclic $b0 $s0 $yes0 $digest0) $gb0 $gs0 $r0)
                     (EBNF:SourceParse
                       (EBNF:EvidenceCyclic $b1 $s1 $yes1 $digest1 $forest) $gb1 $gs1 $r1))
                     (EbnfCyclicForestV1 $yes0 $yes1
                       (== $b0 $b1) (== $s0 $s1) (== $digest0 $digest1)
                       (== $r0 $r1) (> (size-atom $forest) 0)))
                   ($unexpected (EbnfUnexpectedCycleV1 $unexpected))))))))
        ($unexpected (EbnfUnexpectedCycleParserV1 $unexpected))))))

!(ebnf-cycle-test:run "<s> ::= \"\"*\n" "")
!(ebnf-cycle-test:run "<s> ::= \"\"+\n" "")
!(ebnf-cycle-test:run "<s> ::= <a>\n<a> ::= <b> | \"x\"\n<b> ::= <a>\n" "x")
!(ebnf-cycle-test:run "<s> ::= \"(\" (\"\"*) \")\"\n" "()")

; Nullable repetition must finish the independent check on this specimen.
; An incomplete result is not evidence of the claimed cyclic forest.
!(let $parser (bnf:parser-from-ebnf-text "<s> ::= (\"x\"?)*\n" "s")
   (let $plain (bnf:parse-ebnf-text $parser "x")
     (let $evidence (bnf:parse-ebnf-text-evidence $parser "x" 10000)
       (let $_ (bnf:close-ebnf-parser $parser)
         (case ($plain $evidence)
           ((((EBNF:SourceParse (EBNF:Cyclic $b $s $yes $d) $gb $gs $r)
              (EBNF:SourceParse (EBNF:EvidenceCyclic $be $se $yes-e $de $f) $gbe $gse $re))
              (EbnfIndependentCycleCheckV1 $yes (== $d $de) (== $yes $yes-e)))
            ($unexpected (EbnfUnexpectedIndependentCheckV1 $unexpected))))))))

!(ebnf-cycle-test:run "<s> ::= (\"x\"?)*\n" "")
!(ebnf-cycle-test:run "<s> ::= (\"x\"?)+\n" "xx")

; Recursion alone is not a forest cycle: consuming recursion has finite
; derivations for a finite input. Sharing a nullable node is not a cycle.
(= (ebnf-cycle-test:finite $grammar $word)
   (let $parser (bnf:parser-from-ebnf-text $grammar "s")
     (let $result (bnf:parse-ebnf-text-evidence $parser $word 10000)
       (let $_ (bnf:close-ebnf-parser $parser)
         (case $result
           (((EBNF:SourceParse (EBNF:EvidenceAccepted $b $s $d $f $trees) $gb $gs $r)
              EbnfFiniteForestV1)
            ($unexpected (EbnfUnexpectedFiniteV1 $unexpected))))))))
!(ebnf-cycle-test:finite "<s> ::= <s> \"x\" | \"y\"\n" "yxx")
!(ebnf-cycle-test:finite "<s> ::= <a> <a>\n<a> ::= | \"x\"\n" "")
!(ebnf-cycle-test:finite "<s> ::= (\"x\" | \"x\")*\n" "xx")

; An input outside the language stays rejected, not a spurious cycle.
!(let $parser (bnf:parser-from-ebnf-text "<s> ::= \"\"*\n" "s")
   (let $result (bnf:parse-ebnf-text-evidence $parser "x" 10000)
     (let $_ (bnf:close-ebnf-parser $parser)
       (case $result
         (((EBNF:SourceParse (EBNF:EvidenceRejected $b $s 0 $expected $d $f) $gb $gs $r)
            EbnfCyclicGrammarRejectsV1)
          ($unexpected (EbnfUnexpectedRejectionV1 $unexpected)))))))

; Refusing an unproductive grammar is distinct from recognizing a cyclic
; derivation forest for an accepted word.
!(case (bnf:parser-from-ebnf-text "<s> ::= (<s>)+\n" "s")
   (((EBNF:GraphRefused $diagnostics $raw $receipt) EbnfUnproductiveNotCycleV1)
    ($unexpected (EbnfUnexpectedUnproductiveV1 $unexpected))))
