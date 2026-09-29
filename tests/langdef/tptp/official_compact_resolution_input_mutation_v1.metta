!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

; A mutant of the resolution-input view is killed when some fixture reads to
; a different value under it than under the reference.

(= (mutation:fixtures)
   ("cnf(c1,axiom,p(X)|~q(X,f(a))). cnf('Named',hypothesis-assumed,p(X)|X != Y|'Q'(42,1/3,-2.5,\"d\",$true,$$s,b)). cnf(7,axiom,a=b). cnf('cat',axiom,cat)."
    "cnf(c1,axiom,p(X)). fof(f,axiom,p)."))

(= (mutation:values $program $texts)
   (if (== (size-atom $texts) 0)
       ()
       (let ($text $rest) (decons-atom $texts)
         (let $value
              (tptp:source-document->resolution-input-with $program
                (tptp:read-text-document (tptp:open-reader strict) tagged $text))
           (let $tail (mutation:values $program $rest)
             (cons-atom $value $tail))))))

(= (mutation:run $program)
   (case $program
     (((NativeHandle $_kind $_id)
        (let $values (mutation:values $program (mutation:fixtures))
          (let $_ (langdef:close-deterministic-equations $program) $values)))
      ($fault (MutationProgramFailure $fault)))))

(= (mutation:load $path)
   (langdef:load-deterministic-equations (LangDef:EquationSources $path)))

!(let $reference (mutation:run (mutation:load (system:arg 0)))
   (let $mutant (mutation:run (mutation:load (system:arg 1)))
     (println! (TptpCompactResolutionInputMutationV1 (== $reference $mutant) (get-metatype $reference)))))
