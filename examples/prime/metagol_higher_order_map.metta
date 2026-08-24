; Higher-order meta-interpretive learning in ordinary MeTTa: learn a mapped
; two-step relation from typed primitive background knowledge.
;
; The untyped producer considers every primitive pair.  The typed producer
; consults exact declarations while it fills the metarule and therefore never
; constructs pairs whose intermediate types cannot meet.  Both routes still
; submit complete programs to `type:check`; examples choose among the admitted
; programs.  This separates search, type authority, and observational fitness.

!(import! &self ../../lib/ilp/metagol_prime.metta)
!(import! &self ../../lib/ilp/metarules.metta)
!(import! &self ../../lib/ilp/benchmark.metta)
!(import! &self ../../lib/list.metta)

!(bind! &ho-kb (new-space))

; Closed, problem-local constructor instances.  These are intentionally not
; `$`-schemes: matcher variables do not masquerade as lexical Pi binders.
(: MIL.Rel (-> Type Type Type))
(: MIL.List (-> Type Type))
(: MIL.MapOperator Type)
(: MIL.MapOp MIL.MapOperator)
(: MIL.Chain
   (-> (MIL.Rel Number Number)
       (MIL.Rel Number Number)
       (MIL.Rel Number Number)))
(: MIL.HO1
   (-> MIL.MapOperator
       (MIL.Rel Number Number)
       (MIL.Rel (MIL.List Number) (MIL.List Number))))

(: r-succ (MIL.Rel Number Number))
(: r-double (MIL.Rel Number Number))
(: r-reverse (MIL.Rel (MIL.List Number) (MIL.List Number)))
(: r-tail (MIL.Rel (MIL.List Number) (MIL.List Number)))

!(add-atom &ho-kb (: MIL.Rel (-> Type Type Type)))
!(add-atom &ho-kb (: MIL.List (-> Type Type)))
!(add-atom &ho-kb (: MIL.MapOperator Type))
!(add-atom &ho-kb (: MIL.MapOp MIL.MapOperator))
!(add-atom &ho-kb
   (: MIL.Chain
      (-> (MIL.Rel Number Number)
          (MIL.Rel Number Number)
          (MIL.Rel Number Number))))
!(add-atom &ho-kb
   (: MIL.HO1
      (-> MIL.MapOperator
          (MIL.Rel Number Number)
          (MIL.Rel (MIL.List Number) (MIL.List Number)))))
!(add-atom &ho-kb (: r-succ (MIL.Rel Number Number)))
!(add-atom &ho-kb (: r-double (MIL.Rel Number Number)))
!(add-atom &ho-kb
   (: r-reverse (MIL.Rel (MIL.List Number) (MIL.List Number))))
!(add-atom &ho-kb
   (: r-tail (MIL.Rel (MIL.List Number) (MIL.List Number))))

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
   (MIL.Rel (MIL.List Number) (MIL.List Number)))

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
             (MIL.Rel (MIL.List Number) (MIL.List Number)) 100000)
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
             (MIL.Rel (MIL.List Number) (MIL.List Number)) 100000)
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
         (TypeBudget 100000))
       $untyped $typed $trace $consistent)))

(= (ho:learn)
   (let $program (ho:generated)
     (if (type:check
           &ho-kb $program
           (MIL.Rel (MIL.List Number) (MIL.List Number)) 100000)
         $program
         (superpose ()))))

; The higher-order metarule is inspectable language data.
!(mil:metarule map-ibk)

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
   (MIL.Rel (MIL.List Number) (MIL.List Number)))
!(type:check
   &ho-kb
   (MIL.HO1 MIL.MapOp (MIL.Chain r-reverse r-tail))
   (MIL.Rel (MIL.List Number) (MIL.List Number)))

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
