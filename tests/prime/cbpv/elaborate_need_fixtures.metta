; Rows of further need fixtures, elaborated into ContextualCBPV and run by
; lib/prime/cbpv.metta.  Where the source is another fixture, the observed
; answers are copied from its .expected file; the match rows run the draft
; on the same facts here, under other names.  Free logic variables refined
; by matching (need_closure_capture, need_lexical_scope's unify rows), a fact
; that is itself a pattern, reify, the quotation and lifting rows, storage in
; a space (modelled here by a let pattern over the stored atom), the ordered
; observers and the typed demand modes are not elaborated.
;   (Row label agrees elaborated observed)
;   (Effects label agrees final-states expected)
!(import! &self ../../../lib/prime/cbpv.metta)

(= (plain $x)
   (let $meta (get-metatype $x)
     (if (== $meta Expression) (if (== $x ()) () (plain-all $x)) (plain-symbol $x))))
(= (plain-all $items)
   (if (== $items ()) ()
       (let $first (car-atom $items)
         (let $rest (cdr-atom $items)
           (let $p (plain $first) (let $ps (plain-all $rest) (cons-atom $p $ps)))))))
(= (plain-symbol $s) (if (== $s s:+) + (if (== $s s:empty) empty (if (== $s s:superpose) superpose $s))))
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
; Observed answers that hold calls as data are quoted, so that none runs.
(= (row-data $label (quote $e) (quote $observed))
   (let $raw (cbpv:elaborated-bag (quote $e))
     (let $plain (plain-all $raw)
       (let $mine (sort-atom $plain)
         (let $theirs (sort-atom $observed)
           (Row $label (== $mine $theirs) $mine $theirs))))))
; The intents of each world, in request order.
(= (intents $label (quote $e) $expected)
   (let $worlds (collapse (let (Seen $a $s $is) (cbpv:elaborated-worlds (quote $e)) $is))
     (Intents $label (== $worlds $expected) $worlds $expected)))

; ---- need_equation_choice_sharing: one source shared by strict and wildcard
; equations; the wildcard's answer observes the same suspension.
(cbpv:def prime-need:choice-source ()
  (s:let (V old) (s:get-state) (s:let (V ignored) (s:change-state! (s:+ (V old) 1)) (s:superpose (A B)))))
(cbpv:def prime-need:choice (A) (left A))
(cbpv:def prime-need:choice (B) (right B))
(cbpv:def prime-need:choice ((V x)) (wild (V x)))
!(row choice-sharing (quote (prime-need:choice (prime-need:choice-source)))
     ((left A) (right B) (wild A) (wild B)))
!(effects choice-sharing (quote (prime-need:choice (prime-need:choice-source))) (1 1 1 1))

; ---- need_branch_effect_isolation: both siblings inherit the parent state.
(cbpv:def bump-choice ()
  (s:let (V x) (s:superpose (A B))
    (s:let (V old) (s:get-state)
      (s:let (V ignored) (s:change-state! (s:+ (V old) 1)) (Pair (V x) (V old))))))
!(row branch-isolation (quote (s:let (V p) (s:delay (bump-choice)) (s:force (V p))))
     ((Pair A 0) (Pair B 0)))

; ---- need_closure_capture: an explicit suspension of a suspension keeps
; call-time choice.
(cbpv:def prime-need:wrap-twice ((V x)) (s:delay (s:delay (V x))))
!(row wrap-twice (quote (s:force (s:force (prime-need:wrap-twice (s:superpose (A B)))))) (A B))

; ---- need_lexical_scope: a crossing set pins a pattern name to the
; parameter's value; other names stay fresh binders.
(cbpv:def prime-need:match-earned ((V part))
  (cross (part) (s:let (Box (V part)) (Box (Part A B)) Matched)))
!(row match-earned (quote (s:let (V part) (Part A B) (prime-need:match-earned (V part)))) (Matched))
!(row match-earned-other (quote (s:let (V part) (Part A C) (prime-need:match-earned (V part)))) ())

; ---- need_match_pattern_values: a memo cell in a pattern is a value the
; pattern consumes, once per value; written syntax stays data.  The draft
; runs the same program here on ground facts.
(fact 1 one)
(fact 2 two)
(fact (+ 1 1) syn)
(pair 2 b)
(pair (+ 1 1) c)
(= (nq $x) (match &self (fact $x $y) $y){$x})
(= (nqp $x) (match &self (pair $x $y) $y){$x})
(= (nu $x) (unify (fact $x $y) (fact 2 two) $y no))
(= (nl $x) (let (fact $x $y) (fact 2 two) $y){$x})
(= (neither) 1)
(= (neither) 2)
(cbpv:fact facts (fact 1 one))
(cbpv:fact facts (fact 2 two))
(cbpv:fact facts (fact (s:+ 1 1) syn))
(cbpv:fact pairs (pair 2 b))
(cbpv:fact pairs (pair (s:+ 1 1) c))
(cbpv:def q ((V x)) (cross (x) (s:match facts (fact (V x) (V y)) (V y))))
(cbpv:def qp ((V x)) (cross (x) (s:match pairs (pair (V x) (V y)) (V y))))
(cbpv:def u ((V x)) (s:unify (fact (V x) (V y)) (fact 2 two) (V y) no))
(cbpv:def l ((V x)) (cross (x) (s:let (fact (V x) (V y)) (fact 2 two) (V y))))
(cbpv:def either () 1)
(cbpv:def either () 2)
!(row match-cell (quote (q (s:+ 1 1))) (collapse (nq (+ 1 1))))
!(row match-cell-pair (quote (qp (s:+ 1 1))) (collapse (nqp (+ 1 1))))
!(row unify-cell (quote (u (s:+ 1 1))) (collapse (nu (+ 1 1))))
!(row let-cell (quote (l (s:+ 1 1))) (collapse (nl (+ 1 1))))
!(row match-each-value (quote (q (either))) (collapse (nq (neither))))
!(row unify-each-value (quote (u (either))) (collapse (nu (neither))))
!(row match-syntax (quote (s:match facts (fact (s:+ 1 1) (V y)) (V y))) (collapse (match &self (fact (+ 1 1) $y) $y)))
!(row match-value (quote (q 2)) (collapse (nq 2)))

; ---- need_demand_modes: call-time choice through quoted parts; a computed
; pair keeps its independent choices; earned syntax stays data; a retained
; field is demanded by a structural pattern and left alone by a variable,
; once however many clauses inspect it.
(cbpv:def prime-need:head-choice () (quote a))
(cbpv:def prime-need:head-choice () (quote a))
(cbpv:def prime-need:head-choice () (quote b))
(cbpv:def prime-need:head-direct (((quote (V p)))) (quote (V p)))
(cbpv:def prime-need:head-pair ((V p)) (Pair (prime-need:head-direct ((V p))) (prime-need:head-direct ((V p)))))
!(row head-choice (quote (prime-need:head-pair (prime-need:head-choice)))
     ((Pair (quote a) (quote a)) (Pair (quote a) (quote a)) (Pair (quote b) (quote b))))
(cbpv:def prime-need:read-service-id ((V x)) (V x))
!(row read-service (quote (prime-need:read-service-id (Pair (s:superpose (1 2)) (s:superpose (3 4)))))
     ((Pair 1 3) (Pair 1 4) (Pair 2 3) (Pair 2 4)))
!(row-data field-let (quote (s:let (V box) (s:datum (Box (+ 1 2))) (s:let (Box (V field)) (V box) (Field (V field)))))
     (quote ((Field (+ 1 2)))))
!(row-data field-case (quote (s:let (V box) (s:datum (Box (+ 1 2))) (s:case (V box) (((Box (V field)) (Field (V field)))))))
     (quote ((Field (+ 1 2)))))
!(row-data field-unify (quote (s:let (V box) (s:datum (Box (+ 1 2))) (s:unify (V box) (Box (V field)) (Field (V field)) NotBox)))
     (quote ((Field (+ 1 2)))))
(cbpv:def prime-need:pattern-tick ((V label) (V value)) (s:let (V ignored) (s:print (V label)) (V value)))
!(row retained-root
     (quote (s:let (NeedPattern root-wild (V value))
              (NeedPattern root-wild (prime-need:pattern-tick unreachable-root-field 7))
              (s:case (V value) (((V unused) Kept) (7 Strict)))))
     (Kept))
!(intents retained-root
     (quote (s:let (NeedPattern root-wild (V value))
              (NeedPattern root-wild (prime-need:pattern-tick unreachable-root-field 7))
              (s:case (V value) (((V unused) Kept) (7 Strict)))))
     (Nil))
!(row retained-field
     (quote (s:let (NeedPattern field-wild (V value))
              (NeedPattern field-wild (Box (prime-need:pattern-tick unreachable-later-field 7)))
              (s:case (V value) (((Box (V unused)) Kept) ((Box 7) Strict)))))
     (Kept))
!(intents retained-field
     (quote (s:let (NeedPattern field-wild (V value))
              (NeedPattern field-wild (Box (prime-need:pattern-tick unreachable-later-field 7)))
              (s:case (V value) (((Box (V unused)) Kept) ((Box 7) Strict)))))
     (Nil))
!(row field-choices
     (quote (s:let (NeedPattern field-choices (V value))
              (NeedPattern field-choices (Box (s:superpose (7 7 8))))
              (s:case (V value) (((Box 7) Strict) ((Box (V other)) (Fallback (V other)))))))
     (Strict Strict (Fallback 8)))
!(row field-once
     (quote (s:let (NeedPattern field-once (V value))
              (NeedPattern field-once (Box (prime-need:pattern-tick prime-need:pattern-demand-once 8)))
              (s:case (V value) (((Box 7) Impossible) ((Box 8) Kept)))))
     (Kept))
!(intents field-once
     (quote (s:let (NeedPattern field-once (V value))
              (NeedPattern field-once (Box (prime-need:pattern-tick prime-need:pattern-demand-once 8)))
              (s:case (V value) (((Box 7) Impossible) ((Box 8) Kept)))))
     ((Cons prime-need:pattern-demand-once Nil)))
