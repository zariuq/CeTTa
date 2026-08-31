; ===========================================================================
;  rhometta-type-inference-nested  --  RECURSIVE backward chaining with a join,
;  as concurrent rho reactions.  This makes the "one resolution step deep" caveat
;  of rhometta-type-inference concrete: here a sub-proof flows back to its parent
;  reaction along a channel, and the parent join assembles the nested proof term.
;
;  Type-checking is proof search.  For a NESTED application like (f (g x)) the
;  outer application rule has a premise (HasType (g x) Int) that is itself an
;  application: it must be solved by another reaction, and ITS proof term
;  (app pg px) must come back to the outer join to build (app pf (app pg px)).
;
;  How the recursion is delivered (the crux):
;    * each goal is solved by `solve`, which SENDS every proof it finds to a
;      result channel; a parent join receives on that channel.
;    * an APPLICATION goal (g x) is routed to the rule; an ATOMIC goal x is
;      routed to a fact lookup.  Sub-channels are derived from a per-node base so
;      no two nodes collide -- the whole search is ONE rho term, ONE rhometta:run
;      (a nested rhometta:run is not re-entrant -- it stack-overflows).
;    * a goal with no proof still sends a `none` sentinel, so a join never waits
;      on a message that never comes (no dangling rho:recv).  combine maps any
;      non-proof (a sentinel, or a pruned branch) to rho:nil, which the proof
;      projection drops.
;
;  Curry-Howard proof terms stay pure: (: (app pf (app pg px)) (HasType (f (g x)) Bool)).
;  A PLN-style truth value rides outside, in (proved <judgment> <stv>).
;
;  HONEST LIMIT.  This demo intentionally keeps nested sub-derivations
;  deterministic -- each nested subgoal has at most one typing.  A genuine
;  may-set INSIDE a nested call (a compound subgoal with two typings, e.g. an
;  overloaded operator applied under another application) needs replicated
;  fan-in so N sub-proofs can feed one parent continuation.  That fan-in is now
;  available as the pure strict-core pattern rho:patterns:!recv, derived from
;  Meredith--Radestock reflection.  The companion
;  rhometta-type-inference-mayset demo uses that library pattern; primitive
;  persistent receive would be a performance/cleanliness convenience, not an
;  expressiveness requirement.
;
;  Runs on the core build:  make BUILD=core
;     ./cetta --profile extended --lang he <this file>
; ===========================================================================

!(import! &self rhometta)

; --- typing context (Curry-Howard judgments), in &kb -------------------------
!(bind! &kb (new-space))
!(add-atom &kb (: px (HasType x Int)))
!(add-atom &kb (: pg (HasType g (Arr Int Int))))    ; g : Int -> Int
!(add-atom &kb (: pf (HasType f (Arr Int Bool))))   ; f : Int -> Bool
; application elimination (curried, one argument):
!(add-atom &kb (: app (-> (: $pf (HasType $fn (Arr $A $B)))
                          (: $pa (HasType $a $A))
                          (HasType ($fn $a) $B))))

; --- rho plumbing ------------------------------------------------------------
(= (chan $n)
   (if (== $n 0) (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))
(= (unval (rho:quote (rho:val $x))) $x)
(= (fact-proofs (: $prf $concl)) (match &kb (: $prf $concl) (proved (: $prf $concl) (stv 1 0.9))))

; --- the recursive CPS solver ------------------------------------------------
; solve $goal at $depth, SENDING each proof (or one `none` sentinel) to chan $out;
; $b is the base from which this node mints fresh, collision-free sub-channels.
(= (solve (: $prf (HasType $e $ty)) $depth $out $b)
   (case $e
     ((($fn $arg) (solve-app (: $prf (HasType $e $ty)) $depth $out $b))   ; application -> rule
      ($atom      (solve-fact (: $prf (HasType $e $ty)) $out)))))         ; atomic -> fact

(= (solve-fact (: $prf $concl) $out)
   (let $fs (collapse (fact-proofs (: $prf $concl)))
     (if (== $fs ())
         (rho:send (chan $out) (rhometta:eval none))                     ; no fact -> sentinel
         (rho:send (chan $out) (rhometta:eval (superpose $fs))))))        ; facts -> one send, N results

(= (solve-app $goal Z $out $b)
   (rho:send (chan $out) (rhometta:eval none)))                          ; depth exhausted -> sentinel
(= (solve-app (: $prf $concl) (S $d) $out $b)
   (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) $concl))
     (let* (($o1 (+ (* $b 4) 1)) ($o2 (+ (* $b 4) 2)) ($b1 (+ (* $b 4) 3)) ($b2 (+ (* $b 4) 4)))
       (rho:par
         (solve (: $p1 $t1) $d $o1 $b1)                                  ; premise 1 -> chan o1
         (solve (: $p2 $t2) $d $o2 $b2)                                  ; premise 2 -> chan o2
         (rho:recv (chan $o1) $r1
           (rho:recv (chan $o2) $r2
             (rho:send (chan $out)
               (rhometta:eval (combine $rule (unval $r1) (unval $r2))))))))))

; JOIN + guard, TOTAL.  Two proofs -> re-match the rule (shared vars must agree),
; build the nested proof term; anything else (a sentinel, an inconsistent pair)
; -> rho:nil, which the proof projection drops.
(= (combine $rule (proved (: $p1 $t1) $v1) (proved (: $p2 $t2) $v2))
   (let $res (collapse (match &kb (: $rule (-> (: $p1 $t1) (: $p2 $t2) $concl))
                          (proved (: ($rule $p1 $p2) $concl) $v1)))
     (if (== $res ()) rho:nil (car-atom $res))))
(= (combine $rule $a1 $a2) rho:nil)   ; catch-all for sentinels / non-proofs

; --- projections + harness ---------------------------------------------------
(= (count-tuple $t) (if (== $t ()) 0 (+ 1 (count-tuple (cdr-atom $t)))))
(= (proof-of (rho:val (proved $j $tv))) (proved $j $tv))
(= (proof-of $other) (empty))
(= (stuck-of $r)
   (case $r (((rho:val (proved $j $tv)) (empty)) (rho:nil (empty)) ($o $o))))
(= (prove-net $g $d) (rho:par (solve $g $d 1 1) (rho:recv (chan 1) $y (rho:drop $y))))
(= (run-proofs $g $d) (collapse (proof-of (rhometta:run (prove-net $g $d)))))

; sequential nilbc oracle (recursive), over the same &kb.
(= (bc (: $proof $concl) $_) (match &kb (: $proof $concl) (: $proof $concl)))
(= (bc (: ($rule $p1 $p2) $concl) (S $d))
   (let* (((: $rule (-> (: $p1 $t1) (: $p2 $t2) $concl))
           (bc (: $rule (-> (: $p1 $t1) (: $p2 $t2) $concl)) $d))
          ((: $p1 $t1) (bc (: $p1 $t1) $d))
          ((: $p2 $t2) (bc (: $p2 $t2) $d)))
     (: ($rule $p1 $p2) $concl)))

(= (verdict $exp $got) (if (== $exp $got) PASS *FAIL*))
(= (act $tag $exp $g $d $desc)
   (let $ps (run-proofs $g $d)
     (=== $tag (verdict $exp (count-tuple $ps)) expect $exp got (count-tuple $ps) : $desc :: $ps)))
(= (gate $tag $exp $got $desc)
   (=== $tag (verdict $exp $got) expect $exp got $got : $desc))

; ===========================================================================
!(act ACT1-base 1 (: $p (HasType (g x) $T)) (S Z)
   "(g x): one application => 1 typing, proof (app pg px) : Int")

!(act ACT2-nested 1 (: $p (HasType (f (g x)) $T)) (S (S Z))
   "(f (g x)): the (g x) sub-proof flows back to the (f _) join => (app pf (app pg px)) : Bool")

!(act ACT3-deeper 1 (: $p (HasType (f (g (g x))) $T)) (S (S (S Z)))
   "(f (g (g x))): depth-3 nesting => (app pf (app pg (app pg px))) : Bool")

!(act ACT4-neg 0 (: $p (HasType (g f) $T)) (S (S Z))
   "(g f): f is not Int-typeable, so the argument premise fails => 0 typings (clean, no stuck)")

!(gate ACT5-no-stuck 0
   (count-tuple (collapse (stuck-of (rhometta:run (prove-net (: $p (HasType (f (g x)) $T)) (S (S Z)))))))
   "the recursive search leaves NO dangling rho:recv/send/eval-payload")

!(gate ACT6-oracle
   (count-tuple (collapse (bc (: $p (HasType (f (g x)) $T)) (S (S Z)))))
   (count-tuple (run-proofs (: $p (HasType (f (g x)) $T)) (S (S Z))))
   "sequential nilbc oracle agrees on the nested proof count")

!("---- nested Curry-Howard proof terms ----")
!(run-proofs (: $p (HasType (f (g x)) $T)) (S (S Z)))
!(run-proofs (: $p (HasType (f (g (g x))) $T)) (S (S (S Z))))
