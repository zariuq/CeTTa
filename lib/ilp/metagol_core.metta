; Metagol-compatible relation-combinator core for CeTTa/MeTTa.
;
; The semantic hypothesis language is a compressed normal form for common MIL
; metarules.  It is deliberately ordinary MeTTa data:
;
;   primitive relation                 P(A,B) :- q(A,B)
;   (MIL.Id q)                         identity
;   (MIL.Chain q r)                    P(A,B) :- q(A,C), r(C,B)
;   (MIL.Choice q r)                   two-clause predicate invention / union
;   (MIL.Closure q)                    identity + tail-recursive transitive closure
;   (MIL.Map q)                        interpreted higher-order map/3
;
; A program term can be decompiled to the conventional Metagol clause set;
; the runtime evaluator below does not trust or need that rendering.
;
; Prime uses exact problem-local declarations to synthesize only well-typed
; terms.  The generic schema declarations from the earlier prototype do not
; live here: `$` is the matcher's variable discipline, not a lexical type
; binder.  Each example declares the closed constructor instances it actually
; authorizes.  PeTTa uses the same term language with a structural generator;
; example testing is shared.

; ---------- Tiny finite-sequence utilities ----------

; Structural observation used by the learner.  Candidate outputs may have the
; wrong Prime type; comparing their quoted forms makes that a normal failed
; example rather than a typed-equality fault.
(= (mil:same-atom $left $right)
   (== (quote $left) (quote $right)))

(= (mil:list-member $needle ()) False)
(= (mil:list-member $needle $items)
   (let ($head $tail) (decons-atom $items)
     (if (mil:same-atom $needle $head)
         True
         (mil:list-member $needle $tail))))

(= (mil:list-all-equal $expected ()) True)
(= (mil:list-all-equal $expected $items)
   (let ($head $tail) (decons-atom $items)
     (and (mil:same-atom $expected $head)
          (mil:list-all-equal $expected $tail))))

(= (mil:list-nonempty $items)
   (if (mil:same-atom $items ()) False True))

; ---------- Primitive relation registry and execution ----------
;
; A problem-local semantic space contains facts
;
;   (MIL.Primitive rel-name executable-symbol)
;
; The rel-name is the inhabitant used in synthesized programs.  The executable
; symbol is ordinary MeTTa code.  Keeping the two names separate lets a learner
; reason about a stable hypothesis vocabulary without giving search authority to
; arbitrary executable symbols.

(= (mil:run $sem (MIL.Id $program) $input)
   (mil:run $sem $program $input))

(= (mil:run $sem (MIL.Chain $left $right) $input)
   (let $middle (mil:run $sem $left $input)
     (mil:run $sem $right $middle)))

; Two equations intentionally implement relational union.  Multiplicity is
; preserved: if both branches derive the same answer, both occurrences remain.
(= (mil:run $sem (MIL.Choice $left $right) $input)
   (mil:run $sem $left $input))
(= (mil:run $sem (MIL.Choice $left $right) $input)
   (mil:run $sem $right $input))

(= (mil:run $sem (MIL.Map $program) $input)
   (mil:map-run $sem $program $input))

(= (mil:run $sem (MIL.HO1 $operator $program) $input)
   (match $sem (MIL.HOOperator $operator $callable)
     ($callable $sem $program $input)))

; Standard interpreted map is one registered higher-order operator, not a
; privileged branch in the learner.
(MIL.HOOperator MIL.MapOp mil:map-run)

(= (mil:map-run $sem $program ()) ())
(= (mil:map-run $sem $program $items)
   (let ($head $tail) (decons-atom $items)
     (let $mapped-head (mil:run $sem $program $head)
       (let $mapped-tail (mil:map-run $sem $program $tail)
         (cons-atom $mapped-head $mapped-tail)))))

; Finite-state/rational closure.  Every direct edge is an answer.  Re-expansion
; of an already visited state is suppressed, which gives finite graph tasks a
; terminating operational reading even when the source graph has cycles.
(= (mil:run $sem (MIL.Closure $program) $input)
   (mil:closure-run $sem $program $input ($input)))

(= (mil:closure-run $sem $program $input $seen)
   (let $next (mil:run $sem $program $input)
     (mil:closure-next $sem $program $next $seen)))

; Direct reachability answer.
(= (mil:closure-next $sem $program $next $seen)
   $next)

; Recursive expansion only for a fresh state.
(= (mil:closure-next $sem $program $next $seen)
   (if (mil:list-member $next $seen)
       (superpose ())
       (mil:closure-run
         $sem $program $next (cons-atom $next $seen))))

; Primitive fallbacks come last.  Fact-backed relations are the preferred
; representation for genuinely relational BK because absence then means zero
; answers in every dialect rather than an inert residual call.
(= (mil:run $sem $program $input)
   (match $sem (MIL.FactPrimitive $program $relation)
     (match $sem (MIL.Fact $relation $input $output) $output)))

; Executable primitives are useful for arithmetic/list transforms.  Their
; callable contract is relational: unsupported inputs must produce zero results
; (normally `superpose ()`) rather than an inert residual call.  This makes the
; boundary identical in Prime and PeTTa and keeps applicability policy explicit.
(= (mil:run $sem $program $input)
   (match $sem (MIL.Primitive $program $callable)
     ($callable $input)))

; ---------- Example semantics ----------
;
; Positive/negative examples use the functionalized dyadic relation view:
;
;   (MIL.Ex input output)
;
; MIL.Relational permits additional outputs unless they are explicitly listed
; as negatives.  MIL.Functional requires each positive input to have at least
; one result and every result for that input to equal the expected output.

(= (mil:outputs $sem $program $input)
   (collapse (mil:run $sem $program $input)))

(= (mil:positive-covered $sem $program (MIL.Ex $input $output))
   (mil:list-member $output (mil:outputs $sem $program $input)))

(= (mil:positive-functional $sem $program (MIL.Ex $input $output))
   (let $outputs (mil:outputs $sem $program $input)
     (and (mil:list-nonempty $outputs)
          (mil:list-all-equal $output $outputs))))

(= (mil:negative-avoided $sem $program (MIL.Ex $input $output))
   (not (mil:list-member $output (mil:outputs $sem $program $input))))

(= (mil:all-positive $sem $program () MIL.Relational) True)
(= (mil:all-positive $sem $program $examples MIL.Relational)
   (let ($example $rest) (decons-atom $examples)
     (and (mil:positive-covered $sem $program $example)
          (mil:all-positive $sem $program $rest MIL.Relational))))

(= (mil:all-positive $sem $program () MIL.Functional) True)
(= (mil:all-positive $sem $program $examples MIL.Functional)
   (let ($example $rest) (decons-atom $examples)
     (and (mil:positive-functional $sem $program $example)
          (mil:all-positive $sem $program $rest MIL.Functional))))

(= (mil:all-negative $sem $program ()) True)
(= (mil:all-negative $sem $program $examples)
   (let ($example $rest) (decons-atom $examples)
     (and (mil:negative-avoided $sem $program $example)
          (mil:all-negative $sem $program $rest))))

(= (mil:consistent $sem $program (MIL.Examples $positive $negative $mode))
   (and (mil:all-positive $sem $program $positive $mode)
        (mil:all-negative $sem $program $negative)))

; ---------- Program metrics / canonical inspection ----------

(= (mil:program-size (MIL.Id $p))
   (+ 1 (mil:program-size $p)))
(= (mil:program-size (MIL.Chain $p $q))
   (+ 1 (+ (mil:program-size $p) (mil:program-size $q))))
(= (mil:program-size (MIL.Choice $p $q))
   (+ 1 (+ (mil:program-size $p) (mil:program-size $q))))
(= (mil:program-size (MIL.Closure $p))
   (+ 1 (mil:program-size $p)))
(= (mil:program-size (MIL.Map $p))
   (+ 1 (mil:program-size $p)))
(= (mil:program-size (MIL.HO1 $operator $p))
   (+ 1 (mil:program-size $p)))
(= (mil:program-size $primitive)
   (if (== (get-metatype $primitive) Symbol)
       1
       (superpose ())))

; A compact Metagol-style structural explanation.  This is data for humans or
; later MIK elaboration; acceptance remains example/type checking above.
(= (mil:explain (MIL.Id $q))
   (MIL.MetaInstance identity (Q $q)))
(= (mil:explain (MIL.Chain $q $r))
   (MIL.MetaInstance chain (Q $q) (R $r)))
(= (mil:explain (MIL.Choice $q $r))
   (MIL.PredicateInvention
     (MIL.MetaInstance identity (Q $q))
     (MIL.MetaInstance identity (Q $r))))
(= (mil:explain (MIL.Closure $q))
   (MIL.RecursiveProgram
     (MIL.MetaInstance identity (Q $q))
     (MIL.MetaInstance tailrec (Q $q))))
(= (mil:explain (MIL.Map $q))
   (MIL.InterpretedBK map (F $q)))
(= (mil:explain (MIL.HO1 $operator $q))
   (MIL.InterpretedBK $operator (F $q)))
(= (mil:explain $primitive)
   (if (== (get-metatype $primitive) Symbol)
       (MIL.PrimitiveUse $primitive)
       (superpose ())))

; ---------- Metagol-style hypothesis templates ----------
;
; These are program-shape templates, distinct from relation constructors.  They
; are the operational counterpart of a finite metarule basis plus a bounded
; clause budget.  Filling a template searches only second-order predicate
; substitutions; first-order variables remain implicit in MIL.Rel execution.
;
; MIL.T.SharedChoiceChain is the compressed form of the canonical Metagol
; grandparent hypothesis:
;   p(A,B) :- inv(A,C), inv(C,B).
;   inv(A,B) :- q(A,B).
;   inv(A,B) :- r(A,B).
; The repeated Choice term denotes one shared invented relation semantically;
; the explicit AST is intentionally representation-independent.

(= (mil:primitive-name $bias)
   (match $bias (MIL.Primitive $program $callable) $program))
(= (mil:primitive-name $bias)
   (match $bias (MIL.FactPrimitive $program $relation) $program))

(= (mil:instantiate-template $bias MIL.T.Primitive)
   (mil:primitive-name $bias))

(= (mil:instantiate-template $bias MIL.T.Chain)
   (MIL.Chain
     (mil:primitive-name $bias)
     (mil:primitive-name $bias)))

(= (mil:instantiate-template $bias MIL.T.Choice)
   (MIL.Choice
     (mil:primitive-name $bias)
     (mil:primitive-name $bias)))

(= (mil:instantiate-template $bias MIL.T.SharedChoiceChain)
   (let* (($left (mil:primitive-name $bias))
          ($right (mil:primitive-name $bias)))
     (MIL.Chain
       (MIL.Choice $left $right)
       (MIL.Choice $left $right))))

(= (mil:instantiate-template $bias MIL.T.Closure)
   (MIL.Closure (mil:primitive-name $bias)))

(= (mil:instantiate-template $bias MIL.T.Map)
   (MIL.Map (mil:primitive-name $bias)))

(= (mil:instantiate-template $bias MIL.T.MapChain)
   (MIL.Map
     (MIL.Chain
       (mil:primitive-name $bias)
       (mil:primitive-name $bias))))

(= (mil:instantiate-template $bias MIL.T.MapChain3)
   (MIL.Map
     (MIL.Chain
       (mil:primitive-name $bias)
       (MIL.Chain
         (mil:primitive-name $bias)
         (mil:primitive-name $bias)))))

; Generic HO templates: the higher-order operator is first-class data.
(= (mil:instantiate-template $bias (MIL.T.HO1 $operator))
   (MIL.HO1 $operator (mil:primitive-name $bias)))

(= (mil:instantiate-template $bias (MIL.T.HO1Chain $operator))
   (MIL.HO1 $operator
     (MIL.Chain
       (mil:primitive-name $bias)
       (mil:primitive-name $bias))))

(= (mil:instantiate-template $bias (MIL.T.HO1Chain3 $operator))
   (MIL.HO1 $operator
     (MIL.Chain
       (mil:primitive-name $bias)
       (MIL.Chain
         (mil:primitive-name $bias)
         (mil:primitive-name $bias)))))

; A user/LLM/ATP may supply an explicit program skeleton.  Hole symbols are
; filled from the same primitive vocabulary.  This small form is the bridge to
; learned template proposals without granting them acceptance authority.
(= (mil:fill-holes $bias (MIL.Hole $name))
   (mil:primitive-name $bias))
(= (mil:fill-holes $bias (MIL.Chain $a $b))
   (MIL.Chain (mil:fill-holes $bias $a)
              (mil:fill-holes $bias $b)))
(= (mil:fill-holes $bias (MIL.Choice $a $b))
   (MIL.Choice (mil:fill-holes $bias $a)
               (mil:fill-holes $bias $b)))
(= (mil:fill-holes $bias (MIL.Closure $a))
   (MIL.Closure (mil:fill-holes $bias $a)))
(= (mil:fill-holes $bias (MIL.Map $a))
   (MIL.Map (mil:fill-holes $bias $a)))
(= (mil:fill-holes $bias (MIL.HO1 $operator $a))
   (MIL.HO1 $operator (mil:fill-holes $bias $a)))
; Closed primitive symbols pass through unchanged.  Non-symbol composite terms
; without a dedicated skeleton rule fail closed; this prevents a MIL.Hole term
; from surviving as a spurious candidate via the catch-all equation.
(= (mil:fill-holes $bias $closed)
   (if (== (get-metatype $closed) Symbol)
       $closed
       (superpose ())))

; Template metadata is ordinary data, so guidance systems can score it without
; changing the semantics of generated programs.
(= (mil:template-cost MIL.T.Primitive) 1)
(= (mil:template-cost MIL.T.Chain) 3)
(= (mil:template-cost MIL.T.Choice) 3)
(= (mil:template-cost MIL.T.SharedChoiceChain) 7)
(= (mil:template-cost MIL.T.Closure) 2)
(= (mil:template-cost MIL.T.Map) 2)
(= (mil:template-cost MIL.T.MapChain) 4)
(= (mil:template-cost MIL.T.MapChain3) 6)

(= (mil:template-cost (MIL.T.HO1 $operator)) 2)
(= (mil:template-cost (MIL.T.HO1Chain $operator)) 4)
(= (mil:template-cost (MIL.T.HO1Chain3 $operator)) 6)
