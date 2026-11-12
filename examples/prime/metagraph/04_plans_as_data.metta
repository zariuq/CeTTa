; Plans as data: built, inspected, modified, then run.
;
; A plan is a set of atoms in a space: (step NAME CODE) and (needs STEP DEP).
; CODE is a closure that receives the results of the steps it needs, as one
; expression of values.  The planner builds the closures with ordinary `let`
; and `lam`, so a value of the planner's scope (a fee) is written into the
; closure's body.  Stored code is data: reading or taking it never runs it.
; The agent inspects the plan by matching, also inside the code, replaces one
; step, adds one dependency, and then runs the plan by explicit activation:
; (eval CODE) turns the stored closure into a function, which is applied.
; Nothing is proved anywhere.
!(import! &self interaction)

(= (sum-of $xs) (foldl-atom $xs 0 $acc $x (+ $acc $x)))

; ---- building the plan: an invoice
!(bind! &plan (new-space))
!(let $fee 7
   (transform &plan ()
     (, (add-atom &plan (step parts (lam ins 40)))
        (add-atom &plan (step labour (lam ins 60)))
        (add-atom &plan (step subtotal (lam ins (sum-of ins))))
        (add-atom &plan (step shipping (lam ins (+ (sum-of ins) $fee))))
        (add-atom &plan (step invoice (lam ins (total (sum-of ins)))))
        (add-atom &plan (needs subtotal parts))
        (add-atom &plan (needs subtotal labour))
        (add-atom &plan (needs shipping subtotal))
        (add-atom &plan (needs invoice shipping)))))

; ---- inspecting it by matching
; Steps, and what each needs.
!(sort-atom (collapse (match &plan (step $s $code) $s)))
!(sort-atom (collapse (match &plan (needs subtotal $d) (subtotal-needs $d))))
; Steps that need nothing: the roots.
!(sort-atom (collapse (transform &plan (every (, (read (step $s $c)) (absent (needs $s $d)))) (root $s))))
; Who would be affected by a change to subtotal, at any distance.
(= (downstream $plan $s) (match $plan (needs $t $s) $t){$s})
(= (downstream $plan $s) (let $t (match $plan (needs $t $s) $t){$s} (downstream $plan $t)))
!(sort-atom (collapse (downstream &plan subtotal)))
; Inside the code: a closure is an atom, so its body is matched like any other
; atom.  Which steps add a constant to the sum of their inputs, and which?
!(collapse (match &plan (step $s (lam $p (+ (sum-of $q) $k))) (adds $s $k)))
; The stored closure, as data.
!(collapse (match &plan (step shipping $code) $code))

; ---- modifying it: one step replaced, one dependency added
; Shipping becomes express: the old step is taken and the new one made in one
; firing.  The new fee is a value of the agent's scope, written into the code.
!(let $express 15
   (transform &plan (take (step shipping $old))
     (, (add-atom &plan (step shipping (lam ins (+ (sum-of ins) $express))))
        (add-atom &plan (replaced shipping $old)))))
; A discount line, and the dependency that brings it into the subtotal.
!(transform &plan ()
   (, (add-atom &plan (step discount (lam ins -10)))
      (add-atom &plan (needs subtotal discount))))
!(sort-atom (collapse (match &plan (needs subtotal $d) (subtotal-needs $d))))
!(collapse (match &plan (step $s (lam $p (+ (sum-of $q) $k))) (adds $s $k)))
; The replaced closure is still there as data, with its old fee.
!(collapse (match &plan (replaced shipping $old) (was $old)))

; ---- running it
; One firing runs one ready step: a step with no result yet, none of whose
; needs lacks a result.  The `let` sources gather its inputs and activate its
; code; the commit records the result.  `else` ends the run when no step is
; ready.  The trace lists the steps in the order they ran.
(= (inputs-of $plan $s) (collapse (match $plan (, (needs $s $d) (result $d $v)) $v){$s}))
(= (run-plan $plan)
   (else (transform $plan
            (, (read (step $s $code))
               (absent (result $s $done))
               (absent (, (read (needs $s $d)) (absent (result $d $v))))
               (let $ins (inputs-of $plan $s))
               (let $value ((eval $code) $ins)))
            (, (add-atom $plan (result $s $value))
               (cons-atom (ran $s $value) (run-plan $plan))))
         ()))
!(run-plan &plan)
!(collapse (match &plan (result invoice $v) (invoice-result $v)))
; Running again does nothing: every step has its result.
!(run-plan &plan)

; ---- what activation means here
; Read in a firing, the closure is data: applying it leaves the application as
; written, even under eval.  (eval CODE) first, then apply, runs it.
!(transform &plan (read (step shipping $code)) ($code (100)))
!(transform &plan (read (step shipping $code)) (eval ($code (100))))
!(transform &plan (read (step shipping $code)) ((eval $code) (100)))

; ---- a stored rule that acts on another space
; A transform is an atom too.  Stored in a space, it is data: it can be read,
; taken apart by matching (here, to find the space it acts on and look inside
; that space), and run by (eval CODE), as often as the agent chooses.
!(bind! &rules (new-space))
!(let $counter (new-space)
   (let* (($_1 (add-atom $counter (val 41)))
          ($_2 (add-atom &rules (rule bump (transform $counter (take (val $v)) (add-atom $counter (val (+ $v 1))))))))
     stored))
(= (value-under-rule $name)
   (transform &rules (read (rule $name (transform $space $sources $template))) (transform $space (read (val $x)) $x)){$name})
!(value-under-rule bump)
!(transform &rules (read (rule bump $code)) (eval $code))
!(transform &rules (read (rule bump $code)) (eval $code))
!(value-under-rule bump)
