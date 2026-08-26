; Native Prime hypothesis-space generation and example evaluation.
;
; Candidate programs are ordinary inhabitants of the indexed family `hyp`.
; The raw producer is the complete Cartesian bag of primitive declarations;
; the typed producer joins only declarations with an identical middle index.
; Their equality after checked refutations is observed by benchmark.metta.

!(import! &self prime_native_hyp_types.metta)
!(import! &self prime_native_hyp.metta)
!(import! &self prime_native_list_types.metta)
!(import! &self benchmark.metta)

(= (hyp:primitive-declaration $bias $primitives)
   (match $bias
     (: $symbol ($primitives $source-sort $target-sort))
     (hyp:declaration $source-sort $target-sort $symbol)))

; Complete enumerate-then-check baseline.  The later primitive deliberately
; carries its authored source index, so an incompatible middle stays visible
; to `type:check` rather than being silently rewritten into a valid program.
(= (hyp:chain-candidate-untyped $bias $sorts $primitives)
   (let (hyp:declaration $source-sort $middle-sort $earlier-symbol)
        (hyp:primitive-declaration $bias $primitives)
     (let (hyp:declaration $later-source-sort $target-sort $later-symbol)
          (hyp:primitive-declaration $bias $primitives)
       (quote
         (hyp:chain $sorts $primitives
           $source-sort $middle-sort $target-sort
           (hyp:primitive $sorts $primitives
             $source-sort $middle-sort $earlier-symbol)
           (hyp:primitive $sorts $primitives
             $later-source-sort $target-sort $later-symbol))))))

; Type-directed producer.  Shared variables in the declaration patterns are
; the family indices themselves; no candidate is dropped for any other cause.
(= (hyp:chain-candidate-typed
     $bias $sorts $primitives $source-sort $target-sort)
   (let (hyp:declaration $source-sort $middle-sort $earlier-symbol)
        (hyp:primitive-declaration $bias $primitives)
     (let (hyp:declaration $middle-sort $target-sort $later-symbol)
          (hyp:primitive-declaration $bias $primitives)
       (quote
         (hyp:chain $sorts $primitives
           $source-sort $middle-sort $target-sort
           (hyp:primitive $sorts $primitives
             $source-sort $middle-sort $earlier-symbol)
           (hyp:primitive $sorts $primitives
             $middle-sort $target-sort $later-symbol))))))

; A path shape is ordinary data: `hyp:path:one` asks for one primitive and
; `(hyp:path:more shape)` prepends one primitive.  The resulting hypothesis is
; still built solely from `hyp:primitive` and `hyp:chain`; path search adds no
; constructor to the indexed family.
(= (hyp:path:prepend
     $sorts $primitives $source-sort $middle-sort $target-sort $symbol
     (quote $later-program))
   (quote
     (hyp:chain $sorts $primitives
       $source-sort $middle-sort $target-sort
       (hyp:primitive $sorts $primitives
         $source-sort $middle-sort $symbol)
       $later-program)))

(= (hyp:path-candidate-untyped
     $bias $sorts $primitives hyp:path:one)
   (let (hyp:declaration $source-sort $target-sort $symbol)
        (hyp:primitive-declaration $bias $primitives)
     (hyp:path-piece $source-sort $target-sort
       (quote
         (hyp:primitive $sorts $primitives
           $source-sort $target-sort $symbol)))))

(= (hyp:path-candidate-untyped
     $bias $sorts $primitives (hyp:path:more $shape))
   (let (hyp:declaration $source-sort $middle-sort $symbol)
        (hyp:primitive-declaration $bias $primitives)
     (let (hyp:path-piece $later-source-sort $target-sort $later-program)
          (hyp:path-candidate-untyped
            $bias $sorts $primitives $shape)
       (hyp:path-piece $source-sort $target-sort
         (hyp:path:prepend
           $sorts $primitives $source-sort $middle-sort $target-sort
           $symbol $later-program)))))

(= (hyp:path-candidate-typed
     $bias $sorts $primitives
     $source-sort $target-sort hyp:path:one)
   (let (hyp:declaration $source-sort $target-sort $symbol)
        (hyp:primitive-declaration $bias $primitives)
     (quote
       (hyp:primitive $sorts $primitives
         $source-sort $target-sort $symbol))))

(= (hyp:path-candidate-typed
     $bias $sorts $primitives
     $source-sort $target-sort (hyp:path:more $shape))
   (let (hyp:declaration $source-sort $middle-sort $symbol)
        (hyp:primitive-declaration $bias $primitives)
     (let $later-program
          (hyp:path-candidate-typed
            $bias $sorts $primitives $middle-sort $target-sort $shape)
       (hyp:path:prepend
         $sorts $primitives $source-sort $middle-sort $target-sort
         $symbol $later-program))))

; A maximum path shape denotes every positive path length up to that bound.
; It controls search only: hypotheses remain built solely from the indexed
; family's primitive and chain constructors.
(= (hyp:path-shape-within hyp:path:one)
   hyp:path:one)
(= (hyp:path-shape-within (hyp:path:more $bound))
   hyp:path:one)
(= (hyp:path-shape-within (hyp:path:more $bound))
   (hyp:path:more (hyp:path-shape-within $bound)))

(= (hyp:path-candidate-typed-within
     $bias $sorts $primitives $source-sort $target-sort $bound)
   (let $shape (hyp:path-shape-within $bound)
     (hyp:path-candidate-typed
       $bias $sorts $primitives
       $source-sort $target-sort $shape)))

; Forward exploration includes the authored source occurrence at depth zero,
; then every typed positive-length hypothesis up to the bound.  Each result
; retains its target sort, target value, proof occurrence, and program.
(= (hyp:forward-within
     $bias $sorts $primitives $source-sort $input $bound)
   (hyp:frontier $source-sort $input
     (hyp:source-proof $input) hyp:path:zero))
(= (hyp:forward-within
     $bias $sorts $primitives $source-sort $input $bound)
   (let $shape (hyp:path-shape-within $bound)
     (let $program
          (hyp:path-candidate-typed
            $bias $sorts $primitives
            $source-sort $target-sort $shape)
       (let (hyp:edge $target $proof)
            (hyp:run $program $input)
         (hyp:frontier $target-sort $target $proof $program)))))

(= (hyp:forward-target-within
     $bias $sorts $primitives
     $source-sort $target-sort $input $bound)
   (let (hyp:frontier $target-sort $target $proof $program)
        (hyp:forward-within
          $bias $sorts $primitives $source-sort $input $bound)
     (hyp:edge $target $proof)))

(= (hyp:path-candidate-data-untyped
     $bias $sorts $primitives $shape)
   (collapse
     (let (hyp:path-piece $source-sort $target-sort $program)
          (hyp:path-candidate-untyped
            $bias $sorts $primitives $shape)
       (MIL.CandidateData $program))))

(= (hyp:path-candidate-data-typed
     $bias $sorts $primitives $source-sort $target-sort $shape)
   (collapse
     (let $program
          (hyp:path-candidate-typed
            $bias $sorts $primitives
            $source-sort $target-sort $shape)
       (MIL.CandidateData $program))))

(= (hyp:path-candidate-trace
     $bias $sorts $primitives $source-sort $target-sort $shape)
   (collapse
     (let (hyp:path-piece $actual-source-sort $actual-target-sort $program)
          (hyp:path-candidate-untyped
            $bias $sorts $primitives $shape)
       (if (type:check
             (unquote $program)
             (hyp $sorts $primitives $source-sort $target-sort))
           (MIL.CandidateVerdict Established NotChecked $program)
           (MIL.CandidateVerdict Refuted NotChecked $program)))))

(= (hyp:chain-candidate-data-untyped $bias $sorts $primitives)
   (collapse
     (let $program
          (hyp:chain-candidate-untyped $bias $sorts $primitives)
       (MIL.CandidateData $program))))

(= (hyp:chain-candidate-data-typed
     $bias $sorts $primitives $source-sort $target-sort)
   (collapse
     (let $program
          (hyp:chain-candidate-typed
            $bias $sorts $primitives $source-sort $target-sort)
       (MIL.CandidateData $program))))

; Classification consumes the quoted producer occurrence through one explicit
; unquote/quote staging redex.  No executable-shaped candidate is demanded.
(= (hyp:chain-candidate-trace
     $bias $sorts $primitives $source-sort $target-sort)
   (collapse
     (let $program
          (hyp:chain-candidate-untyped $bias $sorts $primitives)
       (if (type:check
             (unquote $program)
             (hyp $sorts $primitives $source-sort $target-sort))
           (MIL.CandidateVerdict Established NotChecked $program)
           (MIL.CandidateVerdict Refuted NotChecked $program)))))

(= (hyp:outputs (quote $program) $input)
   (collapse (hyp:run (quote $program) $input)))

; A checked hypothesis program is an ordinary higher-order relation value.
; The generic List relator therefore remains independent of learning and of
; the `hyp` representation.
(= ((hyp:relation (quote $program)) $input)
   (let (hyp:edge $target $proof)
        (hyp:run (quote $program) $input)
     (rel:edge $target $proof)))

(= (hyp:target-witness (quote $program) $input $target)
   (let (hyp:edge $target $proof)
        (hyp:run (quote $program) $input)
     True))

(= (hyp:positive-covered (quote $program) (hyp:example $input $target))
   (not (==
     (collapse
       (hyp:target-witness (quote $program) $input $target))
     ())))

(= (hyp:negative-avoided (quote $program) (hyp:example $input $target))
   (==
     (collapse
       (hyp:target-witness (quote $program) $input $target))
     ()))

(= (hyp:all-positive (quote $program) ()) True)
(= (hyp:all-positive (quote $program) $examples)
   (let ($example $rest) (decons-atom $examples)
     (if (hyp:positive-covered (quote $program) $example)
         (hyp:all-positive (quote $program) $rest)
         False)))

(= (hyp:all-negative (quote $program) ()) True)
(= (hyp:all-negative (quote $program) $examples)
   (let ($example $rest) (decons-atom $examples)
     (if (hyp:negative-avoided (quote $program) $example)
         (hyp:all-negative (quote $program) $rest)
         False)))

(= (hyp:consistent
     (quote $program) (hyp:examples $positive $negative))
   (if (hyp:all-positive (quote $program) $positive)
       (hyp:all-negative (quote $program) $negative)
       False))

(= (hyp:consistent-chain-candidate-data
     $bias $sorts $primitives $source-sort $target-sort $examples)
   (collapse
     (let $program
          (hyp:chain-candidate-typed
            $bias $sorts $primitives $source-sort $target-sort)
       (if (hyp:consistent $program $examples)
           (MIL.CandidateData $program)
           (superpose ())))))

(= (hyp:consistent-path-candidate-data
     $bias $sorts $primitives
     $source-sort $target-sort $shape $examples)
   (collapse
     (let $program
          (hyp:path-candidate-typed
            $bias $sorts $primitives
            $source-sort $target-sort $shape)
       (if (hyp:consistent $program $examples)
           (MIL.CandidateData $program)
           (superpose ())))))

(= (hyp:learn-chain
     $bias $sorts $primitives $source-sort $target-sort $examples)
   (select
     (let $program
          (hyp:chain-candidate-typed
            $bias $sorts $primitives $source-sort $target-sort)
       (if (hyp:consistent $program $examples)
           $program
           (superpose ())))))

(= (hyp:learn-path
     $bias $sorts $primitives
     $source-sort $target-sort $shape $examples)
   (select
     (let $program
          (hyp:path-candidate-typed
            $bias $sorts $primitives
            $source-sort $target-sort $shape)
       (if (hyp:consistent $program $examples)
           $program
           (superpose ())))))

(= (hyp:chain-benchmark
     $name $mode $bias $sorts $primitives
     $source-sort $target-sort $examples)
   (let* (($untyped
             (hyp:chain-candidate-data-untyped
               $bias $sorts $primitives))
          ($typed
             (hyp:chain-candidate-data-typed
               $bias $sorts $primitives $source-sort $target-sort))
          ($trace
             (hyp:chain-candidate-trace
               $bias $sorts $primitives $source-sort $target-sort))
          ($consistent
             (hyp:consistent-chain-candidate-data
               $bias $sorts $primitives
               $source-sort $target-sort $examples)))
     (mil:bench:summary
       $name $mode $untyped $typed $trace $consistent)))

(= (hyp:path-benchmark
     $name $mode $bias $sorts $primitives
     $source-sort $target-sort $shape $examples)
   (let* (($untyped
             (hyp:path-candidate-data-untyped
               $bias $sorts $primitives $shape))
          ($typed
             (hyp:path-candidate-data-typed
               $bias $sorts $primitives
               $source-sort $target-sort $shape))
          ($trace
             (hyp:path-candidate-trace
               $bias $sorts $primitives
               $source-sort $target-sort $shape))
          ($consistent
             (hyp:consistent-path-candidate-data
               $bias $sorts $primitives
               $source-sort $target-sort $shape $examples)))
     (mil:bench:summary
       $name $mode $untyped $typed $trace $consistent)))
