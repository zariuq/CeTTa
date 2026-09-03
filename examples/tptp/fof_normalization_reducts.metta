; lib_tptp executes the declared FOF-normalization LanguageDef as a relation.
; The positive example needs nested congruence steps.  The negative-polarity
; example computes the semantic negation of truth.  The invalid-source example
; shows that an invented resolved-FOF constructor is rejected before any
; rewrite can run.

!(import! &self lib_tptp)

(= (example:normalization-summary $result)
   (case $result
     (((LangDef:ContextualTermResults LangDef:ContextComplete
         (LangDef:ContextualTermResult
           (tptp-fof-nnf:and
             (tptp-fof-nnf:verum)
             (tptp-fof-nnf:verum))
           $trace))
        (TPTPFofNormalizationExample.NNF
          (tptp-fof-nnf:and
            (tptp-fof-nnf:verum)
            (tptp-fof-nnf:verum))))
      ($_
        (TPTPFofNormalizationExample.NormalizationFailed $result)))))

(= (example:rejection-summary $result)
   (case $result
     (((LangDef:ContextualCodecFault
         LangDef:SourceTerm unknown_constructor $message)
        (TPTPFofNormalizationExample.RejectedInventedConstructor))
      ($_
        (TPTPFofNormalizationExample.RejectionFailed $result)))))

(= (example:negative-normalization-summary $result)
   (case $result
     (((LangDef:ContextualTermResults LangDef:ContextComplete
         (LangDef:ContextualTermResult
           (tptp-fof-nnf:falsum) $trace))
        (TPTPFofNormalizationExample.NegatedTruth
          (tptp-fof-nnf:falsum)))
      ($_
        (TPTPFofNormalizationExample.NegativeNormalizationFailed
          $result)))))

!(example:normalization-summary
   (tptp:resolved-fof->positive-nnf-reducts 8 500000
     (tptp-fof-resolved:not
       (tptp-fof-resolved:implies
         (tptp-fof-resolved:verum)
         (tptp-fof-resolved:falsum)))))

!(example:negative-normalization-summary
   (tptp:resolved-fof->negative-nnf-reducts 8 500000
     (tptp-fof-resolved:verum)))

!(example:rejection-summary
   (tptp:resolved-fof->positive-nnf-reducts 8 500000
     (tptp-fof-resolved:invented)))
