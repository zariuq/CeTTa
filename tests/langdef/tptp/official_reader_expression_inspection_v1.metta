!(import! &self ../../../lib/lib_tptp.metta)

(= (expression-v1:first
      (tptp-rec:file (tptp-rec:inputs-cons $first $_tail) $_span))
   $first)
(= (expression-v1:second
      (tptp-rec:file
        (tptp-rec:inputs-cons $_first
          (tptp-rec:inputs-cons $second $_tail)) $_span))
   $second)
(= (expression-v1:formula (TPTP:RecordFormula $formula)) $formula)
(= (expression-v1:forall-body (tptp-rec:forall $_variables $body)) $body)
(= (expression-v1:first-argument
      (tptp-rec:atom $_head
        (tptp-rec:terms-cons $first $_tail)))
   $first)
(= (expression-v1:arguments (tptp-rec:atom $_head $arguments)) $arguments)

(= (expression-v1:run $file)
   (let $type-expression
        (expression-v1:formula
          (tptp:record-formula (expression-v1:first $file)))
     (let $quantified
          (expression-v1:formula
            (tptp:record-formula (expression-v1:second $file)))
       (let $body (expression-v1:forall-body $quantified)
         (TptpOfficialReaderExpressionInspectionV1
           (tptp:expression-kind $type-expression)
           (tptp:expression-children $type-expression)
           (tptp:expression-kind $quantified)
           (tptp:expression-kind $body)
           (tptp:expression-kind (expression-v1:first-argument $body))
           (tptp:expression-kind
             (expression-v1:arguments $body))
           (tptp:expression-children
             (expression-v1:arguments $body))
           (tptp:expression-kind
             (tptp-rec:terms-cons
               (tptp-rec:var (tptp-rec:word-upper "X"))
               (tptp-rec:terms-nil)))
           (tptp:expression-children
             (tptp-rec:terms-cons
               (tptp-rec:var (tptp-rec:word-upper "X"))
               (tptp-rec:terms-nil)))
           (tptp:expression-kind
             (tptp-rec:vars-cons
               (tptp-rec:typed-var
                 (tptp-rec:var (tptp-rec:word-upper "X"))
                 (tptp-rec:type-defined (tptp-rec:word-defined "$i")))
               (tptp-rec:vars-nil)))
           (tptp:expression-children (tptp-rec:word-lower "leaf"))
           (tptp:expression-kind (not-an-expression))
           (tptp:expression-children (not-an-expression)))))))

!(println!
   (expression-v1:run
     (tptp:read-text
       "tff(p_type,type,p:$i>$o). tff(a,axiom,! [X:$i] : p(X)).")))
