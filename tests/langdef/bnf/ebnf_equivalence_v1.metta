!(import! &self ../../../lib/lib_bnf.metta)

; Independently authored BNF equivalents. This compares acceptance,
; rejection offsets and derivation counts, not raw helper-bearing CSTs.
(= (ebnf-test:observation (LangDef:AuthoredParseAccepted $lang $binding $source $trees))
   (accepted (size-atom $trees)))
(= (ebnf-test:observation (LangDef:AuthoredParseRejected $lang $binding $source $offset $expected))
   (rejected $offset))
(= (ebnf-test:observation (LangDef:AuthoredParseIncomplete $lang $binding $source $outcome $detail))
   (incomplete $outcome))

(= (ebnf-test:compare $extended $plain $words)
   (let $ep (bnf:parser-from-ebnf-text $extended "s")
     (let $bp (bnf:parser-from-text $plain "s")
       (case ($ep $bp)
         ((((EBNF:SourceParser $eh $binding $source $receipt)
            (NativeHandle $kind $id))
            (let $answers
                 (collapse
                   (let $word (superpose $words)
                     (== (ebnf-test:observation (langdef:parse-authored-text $eh $word))
                         (ebnf-test:observation (langdef:parse-authored-text $bp $word)))))
              (let $_ (bnf:close-ebnf-parser $ep)
                (let $_ (langdef:close-authored-parser $bp) $answers))))
          ($unexpected (EbnfUnexpectedPairV1 $unexpected)))))))

!(ebnf-test:compare "<s> ::= (\"a\" | \"b\")*\n"
   "<s> ::= | \"a\" <s> | \"b\" <s>\n"
   ("" "a" "b" "aa" "ab" "ba" "bb" "aab" "x" "ax"))
!(ebnf-test:compare "<s> ::= (\"x\" | \"x\")*\n"
   "<s> ::= | \"x\" <s> | \"x\" <s>\n"
   ("" "x" "xx" "xxx" "y" "xy"))
!(ebnf-test:compare "<s> ::= \"(\" \"a\"+ \")\"?\n"
   "<s> ::= \"(\" \"a\" <tail> <close>\n<tail> ::= | \"a\" <tail>\n<close> ::= | \")\"\n"
   ("" "(" "(a" "(a)" "(aa)" "(aaa" "(b)" "(a))"))
!(ebnf-test:compare "<s> ::= \"\"?\n"
   "<s> ::= |\n"
   ("" "x"))

; Repetition expansion must not duplicate a source's unresolved-reference
; diagnostic. Equal authored references, however, remain two occurrences.
!(case (bnf:validate-ebnf-declarations
         (bnf:document (bnf:read-ebnf-text "<s> ::= <missing>+\n"))
         (bnf:default-authority "s"))
   (((BNFDeclarationValidationRefusedV1
       (BNFDiagnosticsConsV1 (BNFUnresolvedReferenceV1 $owner $name $span) BNFDiagnosticsNilV1))
      (EbnfOneSourceDiagnosticV1 (bnf:text->string $owner) (bnf:text->string $name) $span))
    ($unexpected (EbnfUnexpectedDiagnosticV1 $unexpected))))
!(case (bnf:validate-ebnf-declarations
         (bnf:document (bnf:read-ebnf-text "<s> ::= (<missing> | <missing>)+\n"))
         (bnf:default-authority "s"))
   (((BNFDeclarationValidationRefusedV1
       (BNFDiagnosticsConsV1 (BNFUnresolvedReferenceV1 $owner-a $name-a $span-a)
         (BNFDiagnosticsConsV1 (BNFUnresolvedReferenceV1 $owner-b $name-b $span-b) BNFDiagnosticsNilV1)))
      (EbnfTwoSourceDiagnosticsV1
        (== $owner-a $owner-b) (== $name-a $name-b) $span-a $span-b))
    ($unexpected (EbnfUnexpectedDiagnosticsV1 $unexpected))))
