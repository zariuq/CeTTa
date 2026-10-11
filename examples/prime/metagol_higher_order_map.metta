; Higher-order meta-interpretive learning in ordinary MeTTa: learn a mapped
; two-step relation from typed primitive background knowledge.
;
; The untyped producer considers every primitive pair.  The typed producer
; consults declarations formed in Prime's native universe hierarchy while it
; fills the metarule.  Both routes still submit complete program-code values
; to `type:check`; examples choose among the admitted programs.  A mismatch
; outside this native profile supplies no refutation or pruning authority.

!(import! &self ../../lib/ilp/metagol_prime.metta)
!(import! &self ../../lib/ilp/metarules.metta)
!(import! &self ../../lib/ilp/benchmark.metta)
!(import! &self ../../lib/list.metta)

!(bind! &ho-kb (new-space))

; The code family uses a closed native declaration profile.  MIL.Number is
; its scalar carrier index; the executable primitives still inspect ordinary
; numeric values with the built-in Number type.  Native formation of these
; carriers makes constructor-domain mismatches checked refutations.  Legacy
; HE Type declarations alone would leave those mismatches Undetermined.
; These declarations are closed instances, not matcher-variable Pi schemas.
(: MIL.Number u1)
(: MIL.Rel (-> u1 u1 u1))
(: MIL.List (-> u1 u1))
(: MIL.MapOperator u1)
(: MIL.MapOp MIL.MapOperator)
(: MIL.Chain
   (-> (MIL.Rel MIL.Number MIL.Number)
       (MIL.Rel MIL.Number MIL.Number)
       (MIL.Rel MIL.Number MIL.Number)))
(: MIL.HO1
   (-> MIL.MapOperator
       (MIL.Rel MIL.Number MIL.Number)
       (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number))))

(: r-succ (MIL.Rel MIL.Number MIL.Number))
(: r-double (MIL.Rel MIL.Number MIL.Number))
(: r-reverse (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number)))
(: r-tail (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number)))

!(add-atom &ho-kb (: MIL.Number u1))
!(add-atom &ho-kb (: MIL.Rel (-> u1 u1 u1)))
!(add-atom &ho-kb (: MIL.List (-> u1 u1)))
!(add-atom &ho-kb (: MIL.MapOperator u1))
!(add-atom &ho-kb (: MIL.MapOp MIL.MapOperator))
!(add-atom &ho-kb
   (: MIL.Chain
      (-> (MIL.Rel MIL.Number MIL.Number)
          (MIL.Rel MIL.Number MIL.Number)
          (MIL.Rel MIL.Number MIL.Number))))
!(add-atom &ho-kb
   (: MIL.HO1
      (-> MIL.MapOperator
          (MIL.Rel MIL.Number MIL.Number)
          (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number)))))
!(add-atom &ho-kb (: r-succ (MIL.Rel MIL.Number MIL.Number)))
!(add-atom &ho-kb (: r-double (MIL.Rel MIL.Number MIL.Number)))
!(add-atom &ho-kb
   (: r-reverse (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number))))
!(add-atom &ho-kb
   (: r-tail (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number))))

!(add-atom &ho-kb (MIL.Primitive r-succ ho:succ))
!(add-atom &ho-kb (MIL.Primitive r-double ho:double))
!(add-atom &ho-kb (MIL.Primitive r-reverse ho:reverse))
!(add-atom &ho-kb (MIL.Primitive r-tail ho:tail))
!(add-atom &ho-kb (MIL.HOOperator MIL.MapOp mil:map-run))

(= (ho:succ $x)
   (if (== (get-type $x) Number) (+ $x 1) (superpose ())))
(= (ho:double $x)
   (if (== (get-type $x) Number) (* $x 2) (superpose ())))
(= (ho:reverse $items)
   (if (== (get-metatype $items) Expression)
       (list:reverse $items)
       (superpose ())))
(= (ho:tail $items)
   (if (and (== (get-metatype $items) Expression)
            (not (== $items ())))
       (list:rest $items)
       (superpose ())))

(= (ho:examples)
   (MIL.Examples
     ((MIL.Ex (1 2 3) (3 4 5))
      (MIL.Ex (10 12 33 3 2 1) (12 14 35 5 4 3)))
     ((MIL.Ex (1 2 3) (2 4 6)))
     MIL.Functional))

(= (ho:target-type)
   (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number)))

(= (ho:untyped-candidates)
   (collapse
     (mil:prime:template-candidate
       &ho-kb (MIL.T.HO1Chain MIL.MapOp) (ho:target-type) 100000)))

; Enumerate first, then ask the checker: the semantic control for typed search.
(= (ho:untyped-admitted)
   (collapse
     (let $program
          (mil:prime:template-candidate
            &ho-kb (MIL.T.HO1Chain MIL.MapOp) (ho:target-type) 100000)
       (if (type:check
             &ho-kb $program
             (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number)) 100000)
           $program
           (superpose ())))))

(= (ho:typed-candidates)
   (collapse
     (mil:prime:typed-template-candidate
       &ho-kb (MIL.T.HO1Chain MIL.MapOp) (ho:target-type) 100000)))

(= (ho:untyped-candidate-data)
   (collapse
       (let $program
          (mil:prime:template-candidate
            &ho-kb (MIL.T.HO1Chain MIL.MapOp) (ho:target-type) 100000)
       (MIL.CandidateData $program))))

(= (ho:typed-candidate-data)
   (collapse
       (let $program
          (mil:prime:typed-template-candidate
            &ho-kb (MIL.T.HO1Chain MIL.MapOp) (ho:target-type) 100000)
       (MIL.CandidateData $program))))

(= (ho:consistent-candidate-data)
   (collapse
     (let $program
          (mil:prime:typed-consistent-template-candidate
            &ho-kb &ho-kb (ho:examples)
            (MIL.T.HO1Chain MIL.MapOp) (ho:target-type) 100000)
       (MIL.CandidateData $program))))

; The checker owns the raw candidate before `collapse` materializes the tagged
; occurrence.  Restaging a candidate from a list would demand MIL.Chain first.
; A non-Boolean verdict leaves this candidate trace inert and therefore
; fails the accounting gate instead of being counted as a refutation.
(= (ho:type-trace)
   (collapse
     (let $program
          (mil:prime:template-candidate
            &ho-kb (MIL.T.HO1Chain MIL.MapOp) (ho:target-type) 100000)
       (if (type:check
             &ho-kb $program
             (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number)) 100000)
           (MIL.CandidateVerdict Established NotChecked $program)
           (MIL.CandidateVerdict Refuted NotChecked $program)))))

(= (ho:generated)
   (select
     (mil:prime:typed-consistent-template-candidate
       &ho-kb &ho-kb (ho:examples)
       (MIL.T.HO1Chain MIL.MapOp) (ho:target-type) 100000)))

(= (ho:bench-summary)
   (let* (($untyped (ho:untyped-candidate-data))
          ($typed (ho:typed-candidate-data))
          ($trace (ho:type-trace))
          ($consistent (ho:consistent-candidate-data)))
     (mil:bench:summary
       HigherOrderMap
       (DeclaredMILCalibration
         (CheckerProvenance RuntimeStats)
         (TypingProfile NativeDeclaredCode)
         (TypeBudget 100000))
       $untyped $typed $trace $consistent)))

(= (ho:learn)
   (let $program (ho:generated)
     (if (type:check
           &ho-kb $program
           (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number)) 100000)
         $program
         (superpose ()))))

; The higher-order metarule is inspectable language data.
!(mil:metarule map-ibk)

; Formation is independently checked before claiming refutation-based pruning.
!(type:formed &ho-kb MIL.Number)
!(type:formed &ho-kb (MIL.List MIL.Number))
!(type:formed &ho-kb (MIL.Rel MIL.Number MIL.Number))
!(type:formed &ho-kb
   (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number)))

; Type pruning changes the route, not the admitted candidate set.
!(list:len (ho:untyped-candidates))
!(list:len (ho:untyped-admitted))
!(list:len (ho:typed-candidates))
!(assertEqual (ho:untyped-admitted) (ho:typed-candidates))
!(assertEqual (mil:bench:bag-canaries) True)
!(ho:bench-summary)

; Positive and negative type controls.
!(type:check
   &ho-kb
   (MIL.HO1 MIL.MapOp (MIL.Chain r-succ r-succ))
   (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number)))
!(type:check
   &ho-kb
   (MIL.HO1 MIL.MapOp (MIL.Chain r-reverse r-tail))
   (MIL.Rel (MIL.List MIL.Number) (MIL.List MIL.Number)))

; Positive and negative observational controls.
!(mil:consistent
   &ho-kb
   (MIL.HO1 MIL.MapOp (MIL.Chain r-succ r-succ))
   (ho:examples))
!(mil:consistent
   &ho-kb
   (MIL.HO1 MIL.MapOp (MIL.Chain r-double r-succ))
   (ho:examples))

; Learned program, structural explanation, and an execution witness.
!(ho:learn)
!(let $program (ho:learn) (mil:explain $program))
!(let $program (ho:learn) (mil:run &ho-kb $program (4 7 11)))

; `type:theorem` is a provisional public name.  This native DTT theorem is the
; inner higher-order meaning learned above: applying one endofunction twice.
; A later theorem must connect MIL.HO1/map execution to this proof term.
!(type:theorem mil-twice
   (-> (-> u0 u0) u0 u0)
   (lam (f x) (f (f x))))
!(type:of mil-twice)
