; Prime keeps datum, computation, and observation distinct:
;   Empty   is one ordinary inert datum;
;   (empty) is the computation that produces no occurrences;
;   case's positional third argument observes only a completed-zero source.

!(import! &self system)

(= (prime-empty:nested-zero)
   (empty))

(= (prime-empty:returned-datum)
   Empty)

(= (prime-empty:demanded Empty)
   matched-empty)

; The explicit zero form has no occurrence, including across an equation.
!(assertEqualToResult
   (reify (empty))
   (()))

!(assertEqualToResult
   (reify ())
   ((())))

!(assertEqualToResult
   (superpose ())
   ())

!(assertEqualToResult
   (superpose (()))
   (()))

!(assertEqualToResult
   (case (empty)
     ((a unexpected-a)))
   ())

!(assertEqualToResult
   (case (empty)
     ((a unexpected-a))
     direct-zero)
   (direct-zero))

!(assertEqualToResult
   (case (prime-empty:nested-zero)
     ((a unexpected-a))
     nested-zero)
   (nested-zero))

; Empty is data whether literal, returned, bound, evaluated inside a
; constructor, streamed, or folded.
!(assertEqualToResult
   (prime-empty:returned-datum)
   (Empty))

!(assertEqualToResult
   (case Empty
     ((Empty literal-empty)
      ($value (unexpected-other $value)))
     unexpected-zero)
   (literal-empty))

!(assertEqualToResult
   (case (prime-empty:returned-datum)
     ((Empty returned-empty)
      ($value (unexpected-other $value)))
     unexpected-zero)
   (returned-empty))

!(assertEqualToResult
   (let $value Empty
     (seen $value))
   ((seen Empty)))

!(assertEqualToResult
   (eval (Cons Empty ()))
   ((Cons Empty ())))

!(assertEqualToResult
   (reify (superpose (Empty a)))
   ((Empty a)))

!(assertEqualToResult
   (fold (superpose (Empty a)) () $acc $item
     (Cons $item $acc))
   ((Cons a (Cons Empty ()))))

; The fallback is source-local.  It does not reinterpret a downstream
; mismatch or a selected arm's deliberate zero as source absence.
!(assertEqualToResult
   (case c
     ((a unexpected-a))
     unexpected-fallback)
   ())

!(assertEqualToResult
   (case a
     ((a (empty)))
     unexpected-fallback)
   ())

; A selected conditional arm is still a computation.  In particular, its
; zero must not be reclassified as the ordinary datum Empty by a tail,
; equation, or multi-result let continuation.
!(assertEqualToResult
   (reify (if False accepted (empty)))
   (()))

!(assertEqualToResult
   (reify (if True (empty) accepted))
   (()))

!(assertEqualToResult
   (reify
     (let $candidate (superpose (rejected wanted))
       (if (== $candidate wanted)
           accepted
           (empty))))
   ((accepted)))

!(assertEqualToResult
   (case (if False accepted (empty))
     (($value (unexpected $value)))
     conditional-zero)
   (conditional-zero))

; The similarly positioned datum remains observable.
!(assertEqualToResult
   (reify (if False accepted Empty))
   ((Empty)))

!(assertEqualToResult
   (case c
     ((a unexpected-a)
      ($value (other $value)))
     unexpected-fallback)
   ((other c)))

; Unit is one occurrence and therefore never selects the zero fallback.
!(assertEqualToResult
   (case ()
     ((() unit-answer))
     unexpected-zero)
   (unit-answer))

; Every nonzero source occurrence is dispatched; fallback is not duplicated.
!(assertEqualToResult
   (case (superpose (a b))
     ((a A)
      (b B))
     unexpected-zero)
   (A B))

; Prime's exact occurrence carrier retains sibling faults.  Collection and
; streaming operators may derive success-only views explicitly, but the
; evaluator itself never deletes a fault merely because another branch won.
!(assertEqualToResult
   (reify (superpose ((Error prime-report boom) ok)))
   (((Error prime-report boom) ok)))

!(assertEqualToResult
   (reify
     (let $value (superpose ((Error prime-report boom) ok))
       (seen $value)))
   (((Error prime-report boom) (seen ok))))

!(assertEqualToResult
   (reify
     (case (superpose ((Error prime-report boom) ok))
       (($value (seen $value)))))
   (((Error prime-report boom) (seen ok))))

; The same datum survives optimized streaming, selection, equation matching,
; parallel fan-out, and physical-space paths.  These fixtures guard the less
; obvious evaluator fast paths, not merely the top-level renderer.
!(assertEqualToResult
   (unique Empty)
   (Empty))

!(assertEqualToResult
   (select Empty)
   (Empty))

!(assertEqualToResult
   (chain Empty $value
     (seen $value))
   ((seen Empty)))

!(assertEqualToResult
   (chain (empty) $value
     (unexpected $value))
   ())

!(assertEqualToResult
   (let* (($value Empty))
     (seen $value))
   ((seen Empty)))

!(assertEqualToResult
   (prime-empty:demanded Empty)
   (matched-empty))

!(assertEqualToResult
   (reify (hyperpose (Empty a)))
   ((Empty a)))

; Prime retains `collapse` as a compatibility alias while reify remains the
; canonical occurrence materializer for new code and specifications.
!(assertEqualToResult
   (collapse (superpose (a b)))
   ((a b)))

!(assertEqualToResult
   (bind! &prime-empty-space (new-space))
   (()))

!(assertEqualToResult
   (add-atom &prime-empty-space Empty)
   (()))

!(assertEqualToResult
   (reify (get-atoms &prime-empty-space))
   ((Empty)))

!(assertEqualToResult
   (match &prime-empty-space Empty
     (seen Empty))
   ((seen Empty)))

; Domain absence is ordinary, explanatory data rather than the historical
; cross-cutting Empty sentinel.
!(assertEqualToResult
   (system:arg 0)
   ((CliArgumentNotFound 0)))

!(println! (PrimeEmptyObservationSummary 40 40 0))
