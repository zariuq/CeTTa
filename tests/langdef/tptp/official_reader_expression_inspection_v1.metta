!(import! &self ../../../lib/lib_tptp.metta)

(= (expression-v1:formula (TPTP:RecordFormula $formula)) $formula)
(= (expression-v1:formula-of $record)
   (let $formula (tptp:record-formula $record) (expression-v1:formula $formula)))
(= (expression-v1:body (∀ $_variables $body)) $body)
(= (expression-v1:variables (∀ $variables $_body)) $variables)

; Kinds of compact values read from text, under the mixed and tagged policies,
; and of values that are not formulas.
(= (expression-v1:run $mixed $tagged)
   (let [$type-record $axiom-record $chain-record $number-record] $mixed
     (let $type-expression (expression-v1:formula-of $type-record)
       (let $quantified (expression-v1:formula-of $axiom-record)
         (let $body (expression-v1:body $quantified)
           (let $chain (expression-v1:formula-of $chain-record)
             (let $numbers (expression-v1:formula-of $number-record)
               (let [$tagged-type $tagged-axiom $_tagged-chain $_tagged-numbers] $tagged
                 (let $tagged-quantified (expression-v1:formula-of $tagged-axiom)
                   (let $tagged-body (expression-v1:body $tagged-quantified)
                     (TptpOfficialReaderExpressionInspectionV1
                       (tptp:expression-kind $type-expression)
                       (tptp:expression-children $type-expression)
                       (tptp:expression-kind $quantified)
                       (tptp:expression-kind $body)
                       (tptp:expression-children $body)
                       (tptp:expression-kind (car-atom (tptp:expression-children $body)))
                       (tptp:expression-kind $chain)
                       (tptp:expression-children $chain)
                       (tptp:expression-kind $numbers)
                       (tptp:expression-kind (car-atom (tptp:expression-children $numbers)))
                       (tptp:expression-kind
                         (car-atom (cdr-atom (tptp:expression-children $numbers))))
                       (tptp:expression-kind $tagged-body)
                       (tptp:expression-kind (car-atom $tagged-body))
                       (tptp:expression-kind (car-atom (tptp:expression-children $tagged-body)))
                       (tptp:expression-kind (expression-v1:formula-of $tagged-type))
                       (tptp:expression-kind (rat 1 2))
                       (tptp:expression-kind (real "1.02"))
                       (tptp:expression-kind ())
                       (tptp:expression-kind (§necessary))
                       (tptp:expression-kind $unbound)
                       (tptp:expression-children leaf)
                       (tptp:expression-kind (expression-v1:variables $quantified))
                       (tptp:expression-children (expression-v1:variables $quantified)))))))))))))

!(let $text "tff(p_type,type,p:$i>$o). tff(a,axiom,! [X:$i] : p(X)). fof(c,axiom,p & q & r). tff(n,axiom,f(1/2,2.5) = 3)."
   (let $reader (tptp:open-reader strict)
     (let $mixed (tptp:read-text $reader mixed $text)
       (let $tagged (tptp:read-text $reader tagged $text)
         (println! (expression-v1:run $mixed $tagged))))))
