; Compositional execution contracts shared by the evaluator and its
; accelerators.  Native artifacts are compiled from these presentations;
; the general evaluator remains the semantic fallback outside a generated
; program's admitted fragment.

(gslt relational-may-effect
  (sort Effect)
  (value pure)
  (value relational-query)
  (op join-effect 2)
  (law join-pure-left
    (= (join-effect pure $effect) $effect))
  (law join-pure-right
    (= (join-effect $effect pure) $effect))
  (law join-relational-left
    (= (join-effect relational-query $effect) relational-query))
  (law join-relational-right
    (= (join-effect $effect relational-query) relational-query))
  (rule atom-effect
    (= (effect $atom) pure))
  (rule expression-effect
    (= (effect (Expr $head $children))
       (join-effect
         (head-effect $head)
         (fold join-effect pure (map effect $children)))))
  (rule defined-head-effect
    (= (head-effect $space $head $revision)
       (least-fixed-point
         (equation-dependencies $space $head $revision))))
  (law revision-pins-defined-head-effect
    (implies
      (= (analysis-revision $analysis) (space-revision $space))
      (= (head-effect $analysis $head)
         (head-effect $space $head (space-revision $space)))))
  (law uncertain-head-is-conservative
    (= (head-effect uncertain) relational-query))
  (law inert-symbol-is-pure
    (= (head-effect inert-symbol) pure))
  (law relational-coverage
    (implies (may-run-relational $expression)
             (= (effect $expression) relational-query))))

(gslt streamed-row-disposition
  (sort Disposition)
  (value contribute)
  (value additive-zero)
  (value transport-fault)
  (value consumer-stop)
  (rule malformed-row
    (= (classify-row malformed $cycle-state) transport-fault))
  (rule cyclic-row
    (= (classify-row decoded cyclic) additive-zero))
  (rule valid-row
    (= (classify-row decoded acyclic) contribute))
  (rule stopped-consumer
    (= (after-consumer contribute stop) consumer-stop)))

(gslt observation-profile
  (sort Visibility)
  (value erase)
  (value emit)
  (rule default-progress
    (= (observe default progress) erase))
  (rule diagnostic-progress
    (= (observe diagnostic progress) emit))
  (rule default-determinate-rule-use
    (= (observe default determinate-rule-use) erase))
  (rule diagnostic-determinate-rule-use
    (= (observe diagnostic determinate-rule-use) emit)))

(gslt resource-lifetime
  (sort State)
  (state uninitialized)
  (state live $epoch)
  (state freed $epoch)
  ; These declarations compile into the Atom constructor's compositional
  ; resource summaries and into the consumers' leaf classifiers.  Resource
  ; discovery is therefore an image of the presentation, not a duplicated
  ; recursive checklist in each evaluator.
  (identity-bearing-grounded-kind space)
  (identity-bearing-grounded-kind state)
  (identity-bearing-grounded-kind capture)
  (identity-bearing-grounded-kind foreign)
  (thread-local-resource-symbol capture)
  (transition initialize uninitialized (live $epoch))
  (transition cleanup (live $epoch) (freed $epoch))
  (transition arena-reset (live $epoch) (live (succ $epoch)))
  (transition arena-free (live $epoch) (freed (succ $epoch)))
  (transition arena-reuse (freed $epoch) (live $epoch))
  (law reset-invalidates
    (= (allocation-valid-after arena-reset) False))
  (law free-invalidates
    (= (allocation-valid-after arena-free) False))
  (law resource-summary-composes
    (= (contains-thread-local-resource (Expr $children))
       (any contains-thread-local-resource $children)))
  (law identity-grounded-implies-thread-local
    (implies
      (identity-bearing-grounded-kind $kind)
      (thread-local-resource-kind $kind))))

(gslt determinate-equation-continuation
  (sort Evidence)
  (value singleton-head)
  (value flat-linear-lhs)
  (value range-restricted-rhs)
  (value revision-current)
  (value ground-call)
  (value register-guard)
  (value register-base)
  (value register-tail)
  (value register-recursive)
  (value call-policy-supported)
  (law unsupported-call-policy-falls-back
    (implies
      (not (call-policy-supported $call))
      (= (equation-execution $equation $call) canonical-evaluator)))
  (rule prepared-tail
    (implies
      (all-evidence singleton-head flat-linear-lhs
                    range-restricted-rhs revision-current ground-call
                    call-policy-supported)
      (= (equation-execution $equation $call) prepared-tail)))
  (rule prepared-register-step
    (implies
      (all-evidence singleton-head flat-linear-lhs
                    range-restricted-rhs revision-current ground-call
                    call-policy-supported
                    register-guard register-base register-tail)
      (= (equation-execution $equation $call) prepared-register-step)))
  (rule prepared-register-recursion
    (implies
      (all-evidence singleton-head flat-linear-lhs
                    range-restricted-rhs revision-current ground-call
                    call-policy-supported
                    register-recursive)
      (= (equation-execution $equation $call)
         prepared-register-recursion))))

(gslt total-expression-normalization
  (sort Evidence)
  (value exact-integer-leaf)
  (value total-integer-head)
  (rule exact-integer
    (implies
      (all-evidence exact-integer-leaf total-integer-head)
      (= (normalize-before-delay $expression) value)))
  (law total-normalization-is-unobservable
    (= (effects (normalize-before-delay $expression)) pure)))

(gslt value-allocation
  (sort AllocationClass)
  (value arena-owned-leaf)
  (value implicit-shared)
  (numeric-leaf-kind integer)
  (numeric-leaf-kind float)
  (numeric-leaf-kind big-integer)
  (numeric-leaf-kind rational)
  (rule transient-numeric-leaf
    (= (allocation-class (numeric-leaf $kind)) arena-owned-leaf))
  (rule published-value
    (= (allocation-class (publish $value)) implicit-shared))
  (law arena-owned-leaf-is-not-ambiently-interned
    (= (implicit-intern (numeric-leaf $kind)) False)))

(gslt register-expression-execution
  (sort ResultKind)
  (value exact-integer)
  (value boolean)
  (sort OperandDiscipline)
  (operand-discipline exact-integer-operands)
  (operand-discipline typed-structural-operands)
  (instruction integer-add)
  (instruction integer-subtract)
  (instruction integer-multiply)
  (instruction atom-equal)
  (instruction integer-equal)
  (instruction integer-less)
  (instruction integer-greater)
  (instruction integer-less-equal)
  (instruction integer-greater-equal)
  (instruction-discipline integer-add exact-integer-operands)
  (instruction-discipline integer-subtract exact-integer-operands)
  (instruction-discipline integer-multiply exact-integer-operands)
  (instruction-discipline atom-equal typed-structural-operands)
  (instruction-discipline integer-equal exact-integer-operands)
  (instruction-discipline integer-less exact-integer-operands)
  (instruction-discipline integer-greater exact-integer-operands)
  (instruction-discipline integer-less-equal exact-integer-operands)
  (instruction-discipline integer-greater-equal exact-integer-operands)
  (rule register-expression
    (implies
      (= (register-head-program $head) ($instruction $result-kind))
      (= (execute-register-expression ($head $arguments)) $result-kind)))
  (law register-expression-is-pure
    (= (effects (execute-register-expression $expression)) pure))
  (law register-applicability-is-preserved
    (implies
      (not (operand-discipline-satisfied $instruction $arguments))
      (= (register-execution $instruction $arguments)
         canonical-evaluator)))
  (law transient-applicability-does-not-publish-operands
    (= (publication-set
         (operand-discipline-satisfied $instruction
                                       (nursery-values $arguments)))
       ())))

(gslt determinate-fold-continuation
  (sort Control)
  (value bind)
  (value branch)
  (value evaluate)
  (state (fold-frame $items $index $accumulator bind-step))
  (state (fold-frame $items $index $accumulator evaluate-step))
  (state (fold-frame $items $index $accumulator evacuate-carry))
  (state (fold-frame $items $index $accumulator advance))
  (rule bind-current-step
    (= (step (fold-frame $items $index $accumulator bind-step))
       (fold-frame $items $index $accumulator evaluate-step)))
  (rule evaluate-current-step
    (= (step (fold-frame $items $index $accumulator evaluate-step))
       (fold-frame $items $index $next-accumulator evacuate-carry)))
  (rule evacuate-only-carry
    (= (step (fold-frame $items $index $next-accumulator evacuate-carry))
       (fold-frame $items $index $next-accumulator advance)))
  (rule advance-current-step
    (= (step (fold-frame $items $index $accumulator advance))
       (fold-frame $items (succ $index) $accumulator bind-step)))
  (law carry-is-the-moving-root
    (= (moving-roots (fold-frame $items $index $accumulator $phase))
       ($accumulator)))
  (law prepared-observation-equivalence
    (= (bag-observe (prepared-fold $fold))
       (bag-observe (ordinary-fold $fold))))
  (law pure-multi-result-may-replay
    (implies
      (= (effects $step) pure)
      (= (bag-observe (decline-after-probe $step))
         (bag-observe (ordinary-step $step)))))
  (law effectful-step-never-replays
    (implies
      (not (= (effects $step) pure))
      (= (fold-execution $step) ordinary-fold)))
  (law unused-item-preserves-demand
    (= (demand (prepared-fold $step) $item)
       (demand (ordinary-fold $step) $item))))

(gslt determinate-pure-call-continuation
  (sort CallMode)
  (value eager)
  (value call-by-need)
  (pattern-kind variable)
  (pattern-kind atom)
  (pattern-kind expression)
  (selection-judgment whnf-disjoint)
  (state (call-frame $head $arguments select-clause))
  (state (call-frame $head $arguments demand-argument))
  (state (call-frame $head $arguments execute-body))
  (state (call-frame $head $arguments return-value))
  (state (bind-frame $pattern $value $slots match-pattern))
  (state (bind-frame $pattern $value $slots execute-body))
  (rule eager-argument
    (implies
      (= (call-mode $composition) eager)
      (= (next-call-phase $call) demand-argument)))
  (rule demand-from-pattern
    (implies
      (= (call-mode $composition) call-by-need)
      (= (next-call-phase $call) select-clause)))
  (rule eager-constructor-congruence
    (implies
      (= (call-mode $composition) eager)
      (= (constructor-children $value)
         (map evaluate (constructor-children $value)))))
  (rule need-constructor-whnf
    (implies
      (= (call-mode $composition) call-by-need)
      (= (constructor-children $value)
         (map suspend (constructor-children $value)))))
  (rule unique-clause
    (implies
      (all-evidence revision-current ground-call
                    pure-head unique-match)
      (= (next-call-phase $call) execute-body)))
  (rule whnf-disjoint-patterns
    (implies
      (= (selection-judgment $left $right) whnf-disjoint)
      (= (same-match $left $right) False)))
  (rule unresolved-pattern-overlap
    (implies
      (not (= (selection-judgment $left $right) whnf-disjoint))
      (= (call-execution $call) canonical-evaluator)))
  (rule ambiguous-or-unsupported
    (implies
      (not (all-evidence revision-current ground-call
                         pure-head unique-match))
      (= (call-execution $call) canonical-evaluator)))
  (rule eager-zero-or-fault
    (implies
      (all-evidence (= (call-mode $composition) eager)
                    (zero-or-fault $evaluated-argument))
      (= (call-execution $call) canonical-evaluator)))
  (rule structural-bind
    (implies
      (= (pattern-match $pattern $value $slots)
         (matched $next-slots))
      (= (step (bind-frame $pattern $value $slots match-pattern))
         (bind-frame $pattern $value $next-slots execute-body))))
  (rule bind-mismatch-fails-closed
    (implies
      (= (pattern-match $pattern $value $slots) mismatch)
      (= (bind-execution $pattern $value $slots)
         canonical-evaluator)))
  (rule determinate-tail-reentry
    (implies
      (all-evidence revision-current pure-head unique-match tail-position)
      (= (step (call-frame $head $arguments execute-body))
         (tail-reenter $head $arguments))))
  (law repeated-bind-variable-is-coreferential
    (= (pattern-match (pair $x $x) (pair $left $right) $slots)
       (if (= $left $right)
           (matched (bind-slot $slots $x $left))
           mismatch)))
  (law visible-bind-variable-is-rigid
    (= (pattern-match (pair $outer $fresh) (pair $left $right)
                      (bind-slot $slots $outer $expected))
       (if (= $left $expected)
           (matched (bind-slot $slots $fresh $right))
           mismatch)))
  (law unused-argument-preserves-demand
    (= (demand (prepared-call call-by-need $call) $argument)
       (demand (ordinary-call $call) $argument)))
  (law need-update-path-compression
    (= (evacuate (update-cell $suspension (value $result)))
       (evacuate $result)))
  (law eager-congruence-preserves-inert-head
    (= (head (eager-congruence (constructor $head $children)))
       $head))
  (law closed-call-observation-equivalence
    (= (bag-observe (prepared-call $mode $call))
       (bag-observe (ordinary-call $call)))))

(gslt evaluator-root-frames
  ; The evaluator's C-stack root frames, as generated traversal programs.
  ; Each frame kind declares its fields with field kinds; the generator
  ; emits the evacuation arm per kind.  The collector core defines what a
  ; field kind MEANS (one visitor macro per field kind) and stays
  ; language-ignorant.  A declared field with an unknown field kind is a
  ; generation error; a generated arm with no core visitor macro is a
  ; compile error: completeness is inherited, never checked at runtime.
  (sort FrameKind)
  (root-field-kind strong-atom-slot)
  (root-field-kind strong-atom-span)
  (root-field-kind logical-bindings)
  (root-field-kind outcome-set)
  ; An ephemeron map traces a value exactly when its key was reached by the
  ; strong-root traversal.  Conditional values may expose further keys, so
  ; the generated arm invokes a least-fixed-point visitor.
  (root-field-kind ephemeron-atom-map)
  (frame-kind lexical
    (field atom_io strong-atom-slot)
    (field env logical-bindings)
    (field etype_io strong-atom-slot))
  (frame-kind outcome_set
    (field outcomes outcome-set))
  (frame-kind function_args
    (field orig_arg strong-atom-slot)
    (field arg_type strong-atom-slot)
    (field env logical-bindings)
    (field arg_os outcome-set)
    (field merged logical-bindings)
    (field active logical-bindings)
    (field child logical-bindings))
  (frame-kind function_args_machine
    (field op strong-atom-slot)
    (field orig_args strong-atom-span)
    (field arg_types strong-atom-span)
    (field prefix strong-atom-span)
    (field outcomes outcome-set))
  (frame-kind prime_task
    (field atom strong-atom-slot)
    (field etype strong-atom-slot)
    (field dynamic_env logical-bindings)
    (field seed_env logical-bindings)
    (field target outcome-set))
  (frame-kind prime_frame
    (field atom strong-atom-slot)
    (field etype strong-atom-slot)
    (field strict_source strong-atom-slot)
    (field then_branch strong-atom-slot)
    (field else_branch strong-atom-slot)
    (field ref strong-atom-slot)
    (field normalization_children strong-atom-span)
    (field env logical-bindings)
    (field child outcome-set)
    (field target outcome-set))
  (frame-kind prime_driver
    (field root_target outcome-set))
  (frame-kind prepared_pure_machine
    (field values strong-atom-span)
    (field live_slots strong-atom-span)
    (field runtime_frames strong-atom-span)
    (field thunk_updates ephemeron-atom-map))
  (law continuation-environment-trimming
    (= (moving-roots
         (prepared-pure-frame $continuations $values $slots))
       (union $values
              (slots-live-in $continuations $slots))))
  ; Sealed contract this component implements:
  ;   StoreReachability.omitted_root_loses_cell — omitting a class loses a
  ;     live cell (the mandatory negative fixture, via the skip-arm hook);
  ;   StoreCollectionAdvanced.local_reach_complete — evacuation from
  ;     complete typed roots is exact;
  ;   StoreReachability.evacuate_reach_iff — renaming preserves
  ;     reachability on the live graph.
  (law arms-are-load-bearing
    (implies (skip-arm $kind)
             (exists $program (not (exact $program))))))

(composition cetta-execution-contracts
  (components relational-may-effect streamed-row-disposition
              observation-profile resource-lifetime
              determinate-equation-continuation
              total-expression-normalization
              value-allocation
              register-expression-execution
              determinate-fold-continuation
              determinate-pure-call-continuation
              evaluator-root-frames))

(interpretation cetta-native-execution (of cetta-execution-contracts)
  (query-default pure)
  (query-head match relational-query)
  (query-head get-atoms relational-query)
  (query-head mork:match relational-query)
  (query-head get-type relational-query)
  (query-head get-type-space relational-query)
  (query-fold join-effect)
  (user-head-analysis revision-keyed least-fixed-point)
  (user-head-uncertain relational-query)
  (user-head-inert pure)
  (pull-admission pure)
  (row malformed transport-fault)
  (row cyclic additive-zero)
  (row valid contribute)
  (observation match-chain-progress default erase)
  (observation match-chain-progress diagnostic emit)
  (observation determinate-rule-use default erase)
  (observation determinate-rule-use diagnostic emit)
  (environment diagnostic CETTA_MATCH_CHAIN_TRACE)
  (environment diagnostic-interval CETTA_MATCH_CHAIN_TRACE_INTERVAL)
  (lifetime arena-reset invalidate)
  (lifetime arena-free invalidate)
  (prepared-equation-requires singleton-head flat-linear-lhs
                              range-restricted-rhs revision-current
                              call-policy-supported)
  (prepared-call-requires ground-call)
  (prepared-register-step-requires register-guard register-base register-tail)
  (prepared-register-recursion-requires register-recursive)
  (prepared-register-limit 16)
  (total-integer-head + 2)
  (total-integer-head - 2)
  (total-integer-head * 2)
  (grounded-allocation integer arena-owned-leaf)
  (grounded-allocation float arena-owned-leaf)
  (grounded-allocation big-integer arena-owned-leaf)
  (grounded-allocation rational arena-owned-leaf)
  (register-head + 2 exact-integer integer-add)
  (register-head - 2 exact-integer integer-subtract)
  (register-head * 2 exact-integer integer-multiply)
  (register-head == 2 boolean atom-equal)
  (register-head < 2 boolean integer-less)
  (register-head > 2 boolean integer-greater)
  (register-head <= 2 boolean integer-less-equal)
  (register-head >= 2 boolean integer-greater-equal)
  (register-head numeric-eq 2 boolean integer-equal)
  (fold-control-head let 3 bind)
  (fold-control-head if 3 branch)
  (fold-control-head eval 1 evaluate)
  (fold-control-head function 1 evaluate)
  (owned-local Bindings bindings_init bindings_free))

(fixtures relational-admission
  (defined-wrapper generated-relational-wrapper)
  (wrapper root $relation)
  (wrapper let (let $answer $relation $answer))
  (wrapper superpose (superpose ($relation)))
  (wrapper if (if True $relation no-result))
  (wrapper pure (joined $name $key)))

(fixtures determinate-fold-semantics
  (definition
    (= (generated-fold-count $accumulator $item)
       (+ $accumulator 1)))
  ; A variable in value position is an atom and therefore pure by
  ; atom-effect.  Only a variable in operator position is uncertain.
  (definition
    (= (generated-fold-return-item $ignored $value) $value))
  (definition
    (= (generated-fold-box-number $value)
       (GeneratedFoldNumber (+ $value 1))))
  (definition
    (= (generated-fold-unbox-number (GeneratedFoldNumber $value))
       $value))
  (definition
    (= (generated-fold-dynamic-number $value)
       (let $boxed (generated-fold-box-number $value)
         (generated-fold-unbox-number $boxed))))
  (pure-fold-case pure-sum admitted 3
    (foldl-atom (1 2 3) 0 $accumulator $item
      (eval (+ $accumulator $item)))
    6)
  (pure-fold-case empty-input admitted 0
    (foldl-atom () 11 $accumulator $item
      (eval (+ $accumulator $item)))
    11)
  (pure-fold-case pure-variable-result admitted 2
    (foldl-atom (4 5) 0 $accumulator $item
      (eval (generated-fold-return-item $accumulator $item)))
    5)
  ; The constructor keeps arithmetic quoted until a pattern extracts it into
  ; eval position.  This pins the explicit dynamic-register continuation.
  (pure-fold-case demanded-register-value admitted 2
    (foldl-atom (1 2) 0 $accumulator $item
      (eval
        (+ $accumulator
           (generated-fold-dynamic-number $item))))
    5)
  ; Tagged arithmetic promotes to the generated GMP arm without changing the
  ; exact result.
  (pure-fold-case tagged-overflow-promotes admitted 1
    (foldl-atom (1) 9223372036854775807 $accumulator $item
      (eval (+ $accumulator $item)))
    9223372036854775808)
  ; Structural equality is an Atom program, not numeric equality.  Both
  ; outcomes keep the generated instruction honest.
  (pure-fold-case structural-equality-true admitted 1
    (foldl-atom (same) False $accumulator $item
      (eval (== $item same)))
    True)
  (pure-fold-case structural-equality-false admitted 1
    (foldl-atom (other) True $accumulator $item
      (eval (== $item same)))
    False)
  (fold-case nondeterministic-step fallback 0
    (collapse
      (foldl-atom (1 2) 0 $accumulator $item
        (superpose
          ((+ $accumulator $item)
           (+ $accumulator (* 10 $item))))))
    (3 21 12 30))
  (fold-case effectful-step fallback 0
    (foldl-atom (1 2 3) 0 $accumulator $item
      (let $ignored
        (add-atom &self (generated-fold-seen $item))
        (eval (+ $accumulator $item))))
    6)
  (observation-case effect-occurs-once
    (collapse
      (match &self (generated-fold-seen $item) $item))
    (1 2 3))
  (prime-fold-case unused-faulting-item admitted 1
    (foldl-atom (sentinel (== 1 "unused-fault")) 7
      $accumulator $item
      (eval (generated-fold-count $accumulator $item)))
    9)
  (petta-fold-case failed-step fallback 0
    (collapse
      (foldl-atom (a b) () $accumulator $item (empty)))
    ()))
