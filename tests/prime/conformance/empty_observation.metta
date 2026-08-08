; Prime keeps datum, computation, and observation distinct:
;   Empty   is one ordinary inert datum;
;   (empty) is the computation that produces no occurrences;
;   case's positional third argument observes only a completed-zero source.

(= (prime-empty:nested-zero)
   (empty))

(= (prime-empty:returned-datum)
   Empty)

(= (prime-empty:demanded Empty)
   matched-empty)

; The explicit zero form has no occurrence, including across an equation.
!(assertEqualToResult
   (collapse (empty))
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
   (collapse (superpose (Empty a)))
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
   (let* (($value Empty))
     (seen $value))
   ((seen Empty)))

!(assertEqualToResult
   (prime-empty:demanded Empty)
   (matched-empty))

!(assertEqualToResult
   (collapse (hyperpose (Empty a)))
   ((Empty a)))

!(assertEqualToResult
   (bind! &prime-empty-space (new-space))
   (()))

!(assertEqualToResult
   (add-atom &prime-empty-space Empty)
   (()))

!(assertEqualToResult
   (collapse (get-atoms &prime-empty-space))
   ((Empty)))

!(assertEqualToResult
   (match &prime-empty-space Empty
     (seen Empty))
   ((seen Empty)))

!(println! (PrimeEmptyObservationSummary 26 26 0))
