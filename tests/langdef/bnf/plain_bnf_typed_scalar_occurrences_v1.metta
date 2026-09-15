; Run after the combined discovery artifact. Expectations below follow the
; authored scalar-list rules: head Unicode diagnostic, adjacent order
; diagnostic, then tail diagnostics. Equal occurrences are not deduplicated.
; Construct grammar data directly, independently of the BNF reader.

(= (scalar-oracle:name) (bnf-v1:text-cons 115 (bnf-v1:text-nil)))
(= (scalar-oracle:span) (bnf-v1:source-span 0 1))
(= (scalar-oracle:origin)
   (bnf-v1:lexical-origin "scalar-occurrence-oracle" 9223372036854775807))
(= (scalar-oracle:expression)
   (bnf-v1:expression
     (bnf-v1:alternatives-cons
       (bnf-v1:alternative (bnf-v1:elements-nil) (scalar-oracle:span))
       (bnf-v1:alternatives-nil)) (scalar-oracle:span)))
(= (scalar-oracle:lexicals $scalars)
   (bnf-v1:lexical-declarations-cons
     (bnf-v1:lexical-declaration
       (bnf-v1:text-cons 99 (bnf-v1:text-nil)) "Character"
       (bnf-v1:lexical-points $scalars) "character-token"
       (scalar-oracle:origin))
     (bnf-v1:lexical-declarations-nil)))
(= (scalar-oracle:input $scalars)
   (bnf-v1:grammar-input
     (bnf-v1:document
       (bnf-v1:entries-cons
         (bnf-v1:rule (scalar-oracle:name) (scalar-oracle:expression)
           (scalar-oracle:span))
         (bnf-v1:entries-nil)) (scalar-oracle:span))
     (bnf-v1:grammar-authority
       (bnf-v1:start (scalar-oracle:name))
       (bnf-v1:lexical-environment (scalar-oracle:lexicals $scalars)))))

(= (scalar-oracle:expected $scalars BNFDiagnosticsNilV1)
   (BNFDeclarationValidationAcceptedV1
     (BNFStartSomeV1 (scalar-oracle:name) (scalar-oracle:span))
     (BNFDefinitionsConsV1
       (BNFDefinitionV1 (scalar-oracle:name) (scalar-oracle:expression)
         (scalar-oracle:span)) BNFDefinitionsNilV1)
     (scalar-oracle:lexicals $scalars)))
(= (scalar-oracle:expected $_scalars (BNFDiagnosticsConsV1 $head $tail))
   (BNFDeclarationValidationRefusedV1 (BNFDiagnosticsConsV1 $head $tail)))

(= (scalar-oracle:check $handle $label $scalars $diagnostics)
   (let $input (scalar-oracle:input $scalars)
     (let $value (scalar-oracle:expected $scalars $diagnostics)
       (let $expected
            (quote ((gslt:result:BNFValidateGrammarDiscoveryV1:10 $value)))
         (let $raw (collapse (gslt:entry:BNFValidateGrammarDiscoveryV1:10 $input))
           (let $typed
                (collapse
                  (gslt:admitted-entry:BNFValidateGrammarDiscoveryV1:10 $handle $input))
             (if (== $raw $expected)
               (if (== $typed $expected)
                 (ScalarOccurrenceOracleAcceptedV1 $label)
                 (ScalarOccurrenceOracleDifferenceV1 $label typed $expected $typed))
               (ScalarOccurrenceOracleDifferenceV1 $label raw $expected $raw))))))))

(= (scalar-oracle:pair $first $second)
   (bnf-v1:scalars-cons $first
     (bnf-v1:scalars-cons $second (bnf-v1:scalars-nil))))
(= (scalar-oracle:triple $first $second $third)
   (bnf-v1:scalars-cons $first (scalar-oracle:pair $second $third)))
(= (scalar-oracle:invalid $scalar)
   (BNFInvalidUnicodeScalarV1 $scalar (scalar-oracle:origin)))
(= (scalar-oracle:nonincreasing $first $second)
   (BNFNonIncreasingLexicalScalarsV1 $first $second (scalar-oracle:origin)))

!(let $handle (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
   (progn
     (println! (scalar-oracle:check $handle increasing
       (scalar-oracle:pair 55295 57344) BNFDiagnosticsNilV1))
     (println! (scalar-oracle:check $handle repeated-equal-diagnostics
       (scalar-oracle:triple 65 65 65)
       (BNFDiagnosticsConsV1 (scalar-oracle:nonincreasing 65 65)
         (BNFDiagnosticsConsV1 (scalar-oracle:nonincreasing 65 65)
           BNFDiagnosticsNilV1))))
     (println! (scalar-oracle:check $handle interleaved-diagnostics
       (scalar-oracle:triple 55296 55296 0)
       (BNFDiagnosticsConsV1 (scalar-oracle:invalid 55296)
         (BNFDiagnosticsConsV1 (scalar-oracle:nonincreasing 55296 55296)
           (BNFDiagnosticsConsV1 (scalar-oracle:invalid 55296)
             (BNFDiagnosticsConsV1 (scalar-oracle:nonincreasing 55296 0)
               BNFDiagnosticsNilV1))))))
     (println! (scalar-oracle:check $handle signed-increasing
       (scalar-oracle:pair -9223372036854775808 9223372036854775807)
       (BNFDiagnosticsConsV1 (scalar-oracle:invalid -9223372036854775808)
         (BNFDiagnosticsConsV1 (scalar-oracle:invalid 9223372036854775807)
           BNFDiagnosticsNilV1))))
     (println! (scalar-oracle:check $handle signed-descending
       (scalar-oracle:pair 9223372036854775807 -9223372036854775808)
       (BNFDiagnosticsConsV1 (scalar-oracle:invalid 9223372036854775807)
         (BNFDiagnosticsConsV1
           (scalar-oracle:nonincreasing 9223372036854775807 -9223372036854775808)
           (BNFDiagnosticsConsV1 (scalar-oracle:invalid -9223372036854775808)
             BNFDiagnosticsNilV1)))))
     (langdef:close-language $handle)))
