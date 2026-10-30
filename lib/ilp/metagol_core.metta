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
; wrong Prime type; comparing them as written, with noreduce-eq, makes that a
; normal failed example rather than a typed-equality fault.
(= (mil:same-atom $left $right)
   (noreduce-eq $left $right))

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

; Frozen facts are ordered occurrences, not a set. Only preparation in the
; pruning library grants permission to use their closed-data failure guards.
(= (mil:fact-outputs $rows $program $input)
   (if (== $rows ()) (superpose ())
     (case (car-atom $rows)
       (((MIL.Edge $name $source $output)
         (if (and (mil:same-atom $program $name) (mil:same-atom $input $source))
           $output (superpose ())))
        ($invalid-row (superpose ()))))))
(= (mil:fact-outputs $rows $program $input)
   (if (== $rows ()) (superpose ()) (mil:fact-outputs (cdr-atom $rows) $program $input)))

; A concrete semantic constructor uses ordinary structural dispatch. Space
; primitives keep their original clauses, with no extra helper or case layer.
(= (mil:run (MIL.FrozenFacts $rows) $program $input)
   (mil:fact-outputs $rows $program $input))
(= (mil:run (MIL.FrozenFacts $rows) (MIL.HO1 MIL.MapOp $program) $input)
   (mil:map-run (MIL.FrozenFacts $rows) $program $input))

; Primitive fallbacks come last. Fact-backed relations are the preferred
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

; A type vocabulary is immutable declaration data, not a new space kind or
; executable background. Declaration occurrences retain their multiplicity.
(= (mil:typed-vocabulary $bias)
   (case $bias
     (((MIL.TypeVocabulary $rows) (MIL.TypeVocabulary $rows))
      ($space
       (let $rows (collapse
         (match $space (: $program (MIL.Rel $source $target))
           (MIL.TypeRow $program (MIL.Rel $source $target))))
         (MIL.TypeVocabulary $rows))))))
(= (mil:typed-rows $rows $program $type)
   (if (== $rows ()) (superpose ())
     (case (mil:typed-row-result (car-atom $rows) $program $type)
       (((MIL.Selected $selected) $selected) ($not-a-declaration (superpose ()))))))
(= (mil:typed-rows $rows $program $type)
   (if (== $rows ()) (superpose ()) (mil:typed-rows (cdr-atom $rows) $program $type)))
(= (mil:typed-row-result (MIL.TypeRow $program $type) $program $type) (MIL.Selected $program))

; Normalize the input skeleton before retaining its structural generator.
; Reflection on arbitrary input syntax stays at this preparation boundary.
(= (mil:normalize-skeleton $term)
   (case $term
     (((MIL.Hole $name) (MIL.Skeleton (MIL.Hole $name)))
      ((MIL.Chain $left $right)
       (case (mil:normalize-skeleton $left)
         (((MIL.Skeleton $a)
           (case (mil:normalize-skeleton $right)
             (((MIL.Skeleton $b) (MIL.Skeleton (MIL.Chain $a $b))) ($invalid-right MIL.InvalidSkeleton))))
          ($invalid-left MIL.InvalidSkeleton))))
      ((MIL.Choice $left $right)
       (case (mil:normalize-skeleton $left)
         (((MIL.Skeleton $a)
           (case (mil:normalize-skeleton $right)
             (((MIL.Skeleton $b) (MIL.Skeleton (MIL.Choice $a $b))) ($invalid-choice-right MIL.InvalidSkeleton))))
          ($invalid-choice-left MIL.InvalidSkeleton))))
      ((MIL.Id $inner)
       (case (mil:normalize-skeleton $inner)
         (((MIL.Skeleton $value) (MIL.Skeleton (MIL.Id $value))) ($invalid-id MIL.InvalidSkeleton))))
      ((MIL.Closure $inner)
       (case (mil:normalize-skeleton $inner)
         (((MIL.Skeleton $value) (MIL.Skeleton (MIL.Closure $value))) ($invalid-closure MIL.InvalidSkeleton))))
      ((MIL.Map $inner)
       (case (mil:normalize-skeleton $inner)
         (((MIL.Skeleton $value) (MIL.Skeleton (MIL.Map $value))) ($invalid-map MIL.InvalidSkeleton))))
      ((MIL.HO1 $operator $inner)
       (case (mil:normalize-skeleton $inner)
         (((MIL.Skeleton $value) (MIL.Skeleton (MIL.HO1 $operator $value))) ($invalid-ho MIL.InvalidSkeleton))))
      ($closed
       (if (== (get-metatype $closed) Symbol)
         (MIL.Skeleton (MIL.PrimitiveTerm $closed)) MIL.InvalidSkeleton)))))

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

; Enumerate untrusted program data before the caller checks it. Constructing
; syntax explicitly retains ill-typed candidates for that admission step.
(= (mil:instantiate-template $bias (MIL.T.HO1Chain $operator))
   (let $left (mil:primitive-name $bias)
     (let $right (mil:primitive-name $bias)
       (let $chain (cons-atom MIL.Chain ($left $right))
         (cons-atom MIL.HO1 ($operator $chain))))))

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
(= (mil:hole-lookup $environment $name)
   (if (== $environment ()) MIL.Unbound
     (case (car-atom $environment)
       (((MIL.Binding $key $value)
         (if (mil:same-atom $name $key) (MIL.Bound $value)
           (mil:hole-lookup (cdr-atom $environment) $name)))))))

; One second-order substitution is threaded through the whole skeleton.
; A repeated name reuses its first choice; it does not make another choice.
(= (mil:fill-holes $bias $skeleton)
   (let $choices (collapse (mil:primitive-name $bias))
     (case (mil:normalize-skeleton $skeleton)
       (((MIL.Skeleton $normalized)
         (case (mil:fill-holes-state $choices $normalized ())
           (((MIL.Filling $program $environment) $program))))
        ($invalid-skeleton (superpose ()))))))
(= (mil:primitive-choice $choices)
   (if (== $choices ()) (superpose ()) (car-atom $choices)))
(= (mil:primitive-choice $choices)
   (if (== $choices ()) (superpose ()) (mil:primitive-choice (cdr-atom $choices))))
(= (mil:fill-holes-state $bias (MIL.Hole $name) $environment)
   (case (mil:hole-lookup $environment $name)
     (((MIL.Bound $program) (MIL.Filling $program $environment))
      (MIL.Unbound
       (let $program (mil:primitive-choice $bias)
         (let $next (cons-atom (MIL.Binding $name $program) $environment)
           (MIL.Filling $program $next)))))))
(= (mil:fill-holes-state $bias (MIL.Chain $a $b) $environment)
   (case (mil:fill-holes-state $bias $a $environment)
     (((MIL.Filling $left $middle)
       (case (mil:fill-holes-state $bias $b $middle)
         (((MIL.Filling $right $next) (MIL.Filling (MIL.Chain $left $right) $next))))))))
(= (mil:fill-holes-state $bias (MIL.Choice $a $b) $environment)
   (case (mil:fill-holes-state $bias $a $environment)
     (((MIL.Filling $left $middle)
       (case (mil:fill-holes-state $bias $b $middle)
         (((MIL.Filling $right $next) (MIL.Filling (MIL.Choice $left $right) $next))))))))
(= (mil:fill-holes-state $bias (MIL.Id $a) $environment)
   (case (mil:fill-holes-state $bias $a $environment)
     (((MIL.Filling $program $next) (MIL.Filling (MIL.Id $program) $next)))))
(= (mil:fill-holes-state $bias (MIL.Closure $a) $environment)
   (case (mil:fill-holes-state $bias $a $environment)
     (((MIL.Filling $program $next) (MIL.Filling (MIL.Closure $program) $next)))))
(= (mil:fill-holes-state $bias (MIL.Map $a) $environment)
   (case (mil:fill-holes-state $bias $a $environment)
     (((MIL.Filling $program $next) (MIL.Filling (MIL.Map $program) $next)))))
(= (mil:fill-holes-state $bias (MIL.HO1 $operator $a) $environment)
   (case (mil:fill-holes-state $bias $a $environment)
     (((MIL.Filling $program $next) (MIL.Filling (MIL.HO1 $operator $program) $next)))))
; Closed primitive symbols pass through unchanged.  Non-symbol composite terms
; without a dedicated skeleton rule fail closed; this prevents a MIL.Hole term
; from surviving as a spurious candidate via the catch-all equation.
(= (mil:fill-holes-state $bias (MIL.PrimitiveTerm $closed) $environment)
   (MIL.Filling $closed $environment))

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
