; tests/prime/need_application.metta, elaborated into ContextualCBPV and run
; by lib/prime/cbpv.metta, against the answers the draft gives for it
; (tests/prime/need_application.expected, copied below).  The program is
; written with inert heads: s:superpose, s:empty, s:Error for a checked
; fault, s:diverge for a computation without an answer, s:delay for an
; explicit suspension, s:get-state and s:change-state! for the counter (the
; run starts from 0), s:case, s:let and s:+.  (App F A), (Lam T B) and
; (idx i) are canonical application.  Rows of need_application.metta not
; elaborated here: the structured head (offset), quotation and lifting
; (the sealing question is open), the registry token, function and
; collapse-bind (explicit control), the typed contracts and the recursive
; countdown.
;   (Row label agrees elaborated observed)
;   (Effects label agrees final-states expected): the counter at the end of
;   each world, which counts how often a producer's effect ran.
!(import! &self ../../../lib/prime/cbpv.metta)

(cbpv:def prime-need:dup ((V x)) (Pair (V x) (V x)))
(cbpv:def prime-need:ignore-empty ((V x)) kept-empty)
(cbpv:def prime-need:ignore-fault ((V x)) kept-fault)
(cbpv:def prime-need:ignore-divergence ((V x)) kept-divergence)
(cbpv:def prime-need:named ((V value)) (Named (V value)))
(cbpv:def prime-need:classify (A (V unused)) alpha)
(cbpv:def prime-need:classify ((V x) (V unused)) other)
(cbpv:def prime-need:same ((V x) (V x)) same)
(cbpv:def prime-need:same ((V x) (V y)) any)
(cbpv:def prime-need:box ((Box A) (V unused)) boxed-a)
(cbpv:def prime-need:case-unbox ((V value)) (s:case (V value) (((Box (V inner)) (V inner)))))
(cbpv:def prime-need:canonical-bump ()
  (s:let (V old) (s:get-state) (s:let (V ignored) (s:change-state! (s:+ (V old) 1)) (V old))))
(cbpv:def prime-need:ordinary-bump ()
  (s:let (V old) (s:get-state) (s:let (V ignored) (s:change-state! (s:+ (V old) 1)) (V old))))
(cbpv:def prime-need:effect-pair ((V x)) (Pair (V x) (V x)))
(cbpv:def prime-need:partial (A) partial-a)
(cbpv:def prime-need:overlap ((V x)) overlap-wildcard)
(cbpv:def prime-need:overlap (A) overlap-a)
(cbpv:def prime-need:decision-bump ()
  (s:let (V old) (s:get-state) (s:let (V ignored) (s:change-state! (s:+ (V old) 1)) (s:superpose (A B)))))
(cbpv:def prime-need:decision (A) decision-a)
(cbpv:def prime-need:decision (B) decision-b)

; The answers as the draft prints them: the inert heads by their names.
(= (plain $x)
   (let $meta (get-metatype $x)
     (if (== $meta Expression) (if (== $x ()) () (plain-all $x)) (plain-symbol $x))))
(= (plain-all $items)
   (if (== $items ()) ()
       (let $first (car-atom $items)
         (let $rest (cdr-atom $items)
           (let $p (plain $first) (let $ps (plain-all $rest) (cons-atom $p $ps)))))))
(= (plain-symbol $s) (if (== $s s:empty) empty (if (== $s s:superpose) superpose $s)))

(= (row $label (quote $e) $observed)
   (let $raw (cbpv:elaborated-bag (quote $e))
     (let $plain (plain-all $raw)
       (let $mine (sort-atom $plain)
         (let $theirs (sort-atom $observed)
           (Row $label (== $mine $theirs) $mine $theirs))))))
(= (effects $label (quote $e) $expected)
   (let $worlds (collapse (let (Seen $a $s $is) (cbpv:elaborated-worlds (quote $e)) $s))
     (let $states (sort-atom $worlds)
       (Effects $label (== $states $expected) $states $expected))))

; ---- canonical ABT application (need_application lines 8-12)
!(row app-identity (quote (App (Lam T (idx 0)) (s:superpose (A B)))) (A B))
!(row app-pair (quote (App (Lam T (Pair (idx 0) (idx 0))) (s:superpose (A B)))) ((Pair A A) (Pair B B)))
!(row app-unused-empty (quote (App (Lam T kept-empty) (s:empty))) (kept-empty))
!(row app-unused-fault (quote (App (Lam T kept-fault) (s:Error bad))) (kept-fault))
!(row app-unused-divergence (quote (App (Lam T kept-divergence) (s:diverge))) (kept-divergence))

; ---- ordinary equation application (lines 19-22)
!(row eq-dup (quote (prime-need:dup (s:superpose (A B)))) ((Pair A A) (Pair B B)))
!(row eq-unused-empty (quote (prime-need:ignore-empty (s:empty))) (kept-empty))
!(row eq-unused-fault (quote (prime-need:ignore-fault (s:Error bad))) (kept-fault))
!(row eq-unused-divergence (quote (prime-need:ignore-divergence (s:diverge))) (kept-divergence))
!(row named (quote (prime-need:named intact)) ((Named intact)))

; ---- needed matching per equation (lines 34-43)
!(row classify (quote (prime-need:classify (s:superpose (A B)) (s:Error unused))) (alpha other))
!(row nonlinear (quote (prime-need:same (s:superpose (A B)) A)) (same any))

; ---- matching demands only the inspected path (lines 48-50)
!(row box-empty (quote (prime-need:box (Box (s:superpose (A B))) (s:empty))) (boxed-a))
!(row box-delayed (quote (prime-need:box (Box (s:superpose (A B))) (s:delay (s:empty))))
     (boxed-a (prime-need:box (Box B) (delay (empty)))))

; ---- case selection and closures (lines 67-76)
!(row case-unbox (quote (prime-need:case-unbox (App (Lam T (Box (idx 0))) (s:superpose (A B))))) (A B))
!(row closure (quote (App (App (Lam T (Lam U (idx 1))) (s:superpose (A B))) ignored)) (A B))

; ---- one demanded occurrence, its effect once (lines 81-100)
!(row canonical-effect (quote (App (Lam T (Pair (idx 0) (idx 0))) (prime-need:canonical-bump))) ((Pair 0 0)))
!(effects canonical-effect (quote (App (Lam T (Pair (idx 0) (idx 0))) (prime-need:canonical-bump))) (1))
!(row ambient-state (quote (s:get-state)) (0))
!(row ordinary-effect (quote (prime-need:effect-pair (prime-need:ordinary-bump))) ((Pair 0 0)))
!(effects ordinary-effect (quote (prime-need:effect-pair (prime-need:ordinary-bump))) (1))

; ---- unmatched calls stay as written; overlapping rules (lines 104-111)
!(row partial (quote (prime-need:partial (s:superpose (A B)))) (partial-a (prime-need:partial B)))
!(row overlap (quote (prime-need:overlap (s:superpose (A B)))) (overlap-wildcard overlap-a))

; ---- candidate rules share one call thunk (lines 116-125)
!(row decision (quote (prime-need:decision (prime-need:decision-bump))) (decision-a decision-b))
!(effects decision (quote (prime-need:decision (prime-need:decision-bump))) (1 1))

; ---- an App whose function is not a lambda stays as written (line 166)
!(row residual-app (quote (App Vec (idx 0))) ((App Vec (idx 0))))
