; A Metagol-style learner in ordinary MeTTa: learn grandparent from mother and
; father facts using the shared-choice chain metarule.
;
; The learner keeps three responsibilities separate:
;   1. relational MeTTa enumerates candidate programs;
;   2. `type:check` admits only candidates with the requested relation type;
;   3. positive and negative examples decide which admitted program is useful.
;
; Declarations are deliberately monomorphic and problem-local.  They do not use
; `$` variables as implicit type schemas.  A later explicit-Pi version belongs
; to the polymorphism rung, after variables-as-types are natively authorized.

!(import! &self ../../lib/ilp/metagol_prime.metta)
!(import! &self ../../lib/ilp/metarules.metta)
!(import! &self ../../lib/ilp/benchmark.metta)

!(bind! &gp-kb (new-space))

; The relation vocabulary and the constructor instances this problem admits.
(: MIL.Rel (-> Type Type Type))
(: MIL.Chain
   (-> (MIL.Rel Atom Atom)
       (MIL.Rel Atom Atom)
       (MIL.Rel Atom Atom)))
(: MIL.Choice
   (-> (MIL.Rel Atom Atom)
       (MIL.Rel Atom Atom)
       (MIL.Rel Atom Atom)))

(: r-mother (MIL.Rel Atom Atom))
(: r-father (MIL.Rel Atom Atom))

; The named space is both the search bias and the executable background
; knowledge.  The same exact declarations also appear above in the module
; signature so the caller-local `type:check` sees the same closed vocabulary.
!(add-atom &gp-kb (: MIL.Rel (-> Type Type Type)))
!(add-atom &gp-kb
   (: MIL.Chain
      (-> (MIL.Rel Atom Atom)
          (MIL.Rel Atom Atom)
          (MIL.Rel Atom Atom))))
!(add-atom &gp-kb
   (: MIL.Choice
      (-> (MIL.Rel Atom Atom)
          (MIL.Rel Atom Atom)
          (MIL.Rel Atom Atom))))
!(add-atom &gp-kb (: r-mother (MIL.Rel Atom Atom)))
!(add-atom &gp-kb (: r-father (MIL.Rel Atom Atom)))

!(add-atom &gp-kb (MIL.FactPrimitive r-mother mother))
!(add-atom &gp-kb (MIL.FactPrimitive r-father father))

!(add-atom &gp-kb (MIL.Fact mother i a))
!(add-atom &gp-kb (MIL.Fact mother c f))
!(add-atom &gp-kb (MIL.Fact mother c g))
!(add-atom &gp-kb (MIL.Fact mother f h))
!(add-atom &gp-kb (MIL.Fact father a b))
!(add-atom &gp-kb (MIL.Fact father a c))
!(add-atom &gp-kb (MIL.Fact father b d))
!(add-atom &gp-kb (MIL.Fact father b e))

(= (gp:examples)
   (MIL.Examples
     ((MIL.Ex i b) (MIL.Ex i c) (MIL.Ex a d) (MIL.Ex a e)
      (MIL.Ex a f) (MIL.Ex a g) (MIL.Ex c h))
     ((MIL.Ex a b) (MIL.Ex b c) (MIL.Ex c d) (MIL.Ex d e)
      (MIL.Ex e f) (MIL.Ex f g) (MIL.Ex g h) (MIL.Ex h i))
     MIL.Relational))

(= (gp:untyped-candidates)
   (collapse
     (mil:prime:template-candidate
       &gp-kb MIL.T.SharedChoiceChain (MIL.Rel Atom Atom) 100000)))

(= (gp:typed-candidates)
   (collapse
     (mil:prime:typed-template-candidate
       &gp-kb MIL.T.SharedChoiceChain (MIL.Rel Atom Atom) 100000)))

; Benchmark bags carry quoted programs uniformly.  This makes hypothesis data
; inspectable without asking an accounting function to execute it.
(= (gp:untyped-candidate-data)
   (collapse
       (let $program
          (mil:prime:template-candidate
            &gp-kb MIL.T.SharedChoiceChain (MIL.Rel Atom Atom) 100000)
       (MIL.CandidateData $program))))

(= (gp:typed-candidate-data)
   (collapse
       (let $program
          (mil:prime:typed-template-candidate
            &gp-kb MIL.T.SharedChoiceChain (MIL.Rel Atom Atom) 100000)
       (MIL.CandidateData $program))))

(= (gp:consistent-candidate-data)
   (collapse
     (let $program
          (mil:prime:typed-consistent-template-candidate
            &gp-kb &gp-kb (gp:examples)
            MIL.T.SharedChoiceChain (MIL.Rel Atom Atom) 100000)
       (MIL.CandidateData $program))))

; Classify at the producer occurrence, before an executable-shaped program is
; materialized inside a list and could be demanded as an ordinary argument.
; This benchmark is deliberately the closed decidable fragment: a non-Boolean
; public judgment leaves the branch inert, so the accounting gate cannot pass
; by silently treating abstention or resource incompleteness as rejection.
(= (gp:type-trace)
   (let* (($mm-status
            (if (type:check
                  &gp-kb
                  (MIL.Chain
                    (MIL.Choice r-mother r-mother)
                    (MIL.Choice r-mother r-mother))
                  (MIL.Rel Atom Atom))
                Established Refuted))
          ($mf-status
            (if (type:check
                  &gp-kb
                  (MIL.Chain
                    (MIL.Choice r-mother r-father)
                    (MIL.Choice r-mother r-father))
                  (MIL.Rel Atom Atom))
                Established Refuted))
          ($fm-status
            (if (type:check
                  &gp-kb
                  (MIL.Chain
                    (MIL.Choice r-father r-mother)
                    (MIL.Choice r-father r-mother))
                  (MIL.Rel Atom Atom))
                Established Refuted))
          ($ff-status
            (if (type:check
                  &gp-kb
                  (MIL.Chain
                    (MIL.Choice r-father r-father)
                    (MIL.Choice r-father r-father))
                  (MIL.Rel Atom Atom))
                Established Refuted)))
     ((MIL.CandidateVerdict $mm-status NotChecked
        (MIL.Chain
          (MIL.Choice r-mother r-mother)
          (MIL.Choice r-mother r-mother)))
      (MIL.CandidateVerdict $mf-status NotChecked
        (MIL.Chain
          (MIL.Choice r-mother r-father)
          (MIL.Choice r-mother r-father)))
      (MIL.CandidateVerdict $fm-status NotChecked
        (MIL.Chain
          (MIL.Choice r-father r-mother)
          (MIL.Choice r-father r-mother)))
      (MIL.CandidateVerdict $ff-status NotChecked
        (MIL.Chain
          (MIL.Choice r-father r-father)
          (MIL.Choice r-father r-father))))))

(= (gp:generated)
   (select
     (mil:prime:typed-consistent-template-candidate
       &gp-kb &gp-kb (gp:examples)
       MIL.T.SharedChoiceChain (MIL.Rel Atom Atom) 100000)))

(= (gp:bench-summary)
   (let* (($untyped (gp:untyped-candidate-data))
          ($typed (gp:typed-candidate-data))
          ($trace (gp:type-trace))
          ($consistent (gp:consistent-candidate-data)))
     (mil:bench:summary
       Grandparent
       (DeclaredMILCalibration
         (CheckerProvenance RuntimeStats)
         UnboundedTypeCheck)
       $untyped $typed $trace $consistent)))

(= (gp:learn)
   (let $program (gp:generated)
     (let $accepted
          (type:check $program (MIL.Rel Atom Atom) 100000)
       (if $accepted
           $program
           (superpose ())))))

; The metarule is inspectable data, not a hidden learner convention.
!(mil:metarule chain)

; Positive and negative typing controls.
!(type:check
   &gp-kb
   (MIL.Chain r-mother r-father)
   (MIL.Rel Atom Atom))
!(type:check
   &gp-kb
   (MIL.Chain r-mother r-father)
   (MIL.Rel Atom Atom)
   100000)
!(type:check
   &gp-kb
   (MIL.Chain r-mother r-father)
   (MIL.Rel Number Number))

; Type-directed generation must preserve the complete candidate set for this
; finite template.  It changes search order, never the meaning of the search.
!(collapse (mil:primitive-name &gp-kb))
!(gp:untyped-candidates)
!(gp:typed-candidates)
!(assertEqual (gp:untyped-candidates) (gp:typed-candidates))
!(gp:bench-summary)

; The example relation rejects a plausible but wrong program.
!(type:check
   &gp-kb
   (MIL.Chain
     (MIL.Choice r-mother r-father)
     (MIL.Choice r-mother r-father))
   (MIL.Rel Atom Atom))
!(mil:consistent
   &gp-kb
   (MIL.Chain
     (MIL.Choice r-mother r-father)
     (MIL.Choice r-mother r-father))
   (gp:examples))
!(mil:consistent &gp-kb (MIL.Chain r-mother r-mother) (gp:examples))

; The learned program, its Metagol-style explanation, and two derived answers.
!(gp:learn)
!(let $program (gp:learn) (mil:explain $program))
!(let $program (gp:learn) (collapse (mil:run &gp-kb $program i)))

; `type:theorem` is a provisional public name.  This is the DTT meaning of the
; chain metarule: compose two endofunctions.  Connecting the learned MIL program
; term itself to this proof is the later explicit-Pi/schema-elaboration rung.
!(type:theorem mil-chain
   (-> (-> u0 u0) (-> u0 u0) (-> u0 u0))
   (lam (q r x) (r (q x))))
!(type:of mil-chain)
