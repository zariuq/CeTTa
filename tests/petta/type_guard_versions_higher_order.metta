; Versions of typed functions that call other typed functions, and of a
; higher-order chainer specialized to its function arguments.  The make target
; compares every answer with the complete protocol
; (CETTA_PETTA_VERSIONS_REFERENCE=1) on both equation routes.

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: dec (-> Nat Nat))
(= (dec Z) Z)
(= (dec (S $k)) $k)
(: is-zero (-> Nat Bool))
(= (is-zero Z) True)
(= (is-zero (S $k)) False)
(: upd (-> $a Nat Nat))
(= (upd $q $d) (dec $d))
(: stop (-> $a Nat Bool))
(= (stop $q $d) (is-zero $d))

!(bind! &hokb (new-space))
!(add-atom &hokb (link a b))
!(add-atom &hokb (link b c))
!(add-atom &hokb (link c d))

; A chainer over its function arguments, returning its query as unification
; leaves it: the specializer gives it static calls, and its version enters
; each callee's version.
(: hop (-> (-> $a $ct $ct) (-> $a $ct Bool) $ct $a $a))
(= (hop $u $t $c (reach $x $y))
   (if ($t (reach $x $y) $c)
       (empty)
       (match &hokb (link $x $z)
              (let (reach $z $y) (hop $u $t ($u (reach $x $y) $c) (reach $z $y))
                (reach $x $y)))))
(= (hop $u $t $c (reach $x $x)) (reach $x $x))

; Positive: depths that cut the chain at each length.
!(collapse (hop upd stop (S (S (S (S Z)))) (reach a $y)))
!(collapse (hop upd stop (S (S Z)) (reach a $y)))
!(collapse (hop upd stop Z (reach a $y)))

; A function argument of the wrong type fixes the context type to Bool:
; its guard refuses Nat contexts, so the chainer has no answer.
(: bad-upd (-> $a Nat Bool))
(= (bad-upd $q $d) True)
!(collapse (hop bad-upd stop (S (S Z)) (reach a $y)))

; A callee whose equations return a value outside its result type in one
; case: its result guard filters that answer on every route.
(: shaky (-> Nat Nat))
(= (shaky Z) Z)
(= (shaky (S $k)) not-a-nat)
(: shaky-upd (-> $a Nat Nat))
(= (shaky-upd $q $d) (shaky $d))
!(collapse (hop shaky-upd stop (S (S (S Z))) (reach a $y)))

; An open context is no ground Nat: the protocol runs, whose guards accept a
; variable and whose callees then match both of their equations.
!(collapse (hop upd stop $open (reach a $y)))

; A function with a type variable shared by two parameters keeps its
; protocol: the coupled guards decide.
(: same (-> $a $a Bool))
(= (same $l $r) (case $r (($l True) ($_ False))))
(: same-stop (-> $a Nat Bool))
(= (same-stop $q $d) (same $d Z))
!(collapse (hop upd same-stop (S (S (S Z))) (reach a $y)))

; A constructor whose type variable couples its fields does not type an
; application of it: the call keeps its guard, which refuses the pair.
(: RP (-> $t $t RPT))
(: rp-check (-> RPT Bool))
(= (rp-check $p) True)
(: rp-stop (-> $a Nat Bool))
(= (rp-stop $q $d) (not (rp-check (RP $d wrong-kind))))
!(collapse (hop upd rp-stop (S (S Z)) (reach a $y)))

; A callee answering outside its result type steers a condition: its result
; guard removes that answer before the condition reads it.
(: classify (-> Nat Bool))
(= (classify Z) True)
(= (classify (S $k)) wrong-bool)
(: classify-stop (-> $a Nat Bool))
(= (classify-stop $q $d) (if (classify $d) True False))
!(collapse (hop upd classify-stop (S (S (S Z))) (reach a $y)))

; A declared symbol among the matched rows: the space is not uninterpreted,
; the version is refused at entry and the protocol answers.
(: d Nat)
!(collapse (hop upd stop (S (S (S (S Z)))) (reach a $y)))

; Rows added later are judged again at the next entry.
!(remove-atom &self (: d Nat))
!(add-atom &hokb (link d e))
!(collapse (hop upd stop (S (S (S (S (S Z))))) (reach a $y)))

; A function argument that may raise is called as the dispatch it was, under
; the dispatch's handler: the specializer writes `(reduce (ctx-stop ...))`.
; The version reads that dispatch as the call it holds, so the chainer keeps
; its version (the make target reads the verdict); a goal the stop names
; cuts the chain at once.
(: Ctx Type)
(: MkCtx (-> $a Nat Ctx))
(: ctx-upd (-> $a Ctx Ctx))
(= (ctx-upd $q (MkCtx $g $d)) (MkCtx $g (dec $d)))
(: ctx-stop (-> $a Ctx Bool))
(= (ctx-stop $q (MkCtx $g $d)) (or (is-zero $d) (== $g stop-here)))
!(collapse (hop ctx-upd ctx-stop (MkCtx goal (S (S Z))) (reach a $y)))
!(collapse (hop ctx-upd ctx-stop (MkCtx stop-here (S (S Z))) (reach a $y)))
