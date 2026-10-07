; A written quotation is a scope, and each opening is an activation (stage 5,
; item 3).  The names a quotation owns, the names written directly in its
; code that no scope around it writes, by the rule a template's are owned
; by, are recorded in it as hidden metadata.  Each opening of the code, `*`
; or the instance `lift let` makes, copies them; under the per-call lifetime
; each opening has names of its own.  A quotation built at run time owns
; nothing.  A name written outside the quotation is shared with it.
!(import! &self template)

; ---- template:map equals map-atom on templates that have locals.
!(row template-map)
!(template:map (1 2) k @(let $u k (Pair $u $u)))
!(row map-atom)
!(map-atom (1 2) $e (let $u $e (Pair $u $u)))
!(row template-map-equals-map-atom)
!(== (template:map (1 2) k @(let $u k (Pair $u $u)))
     (map-atom (1 2) $e (let $u $e (Pair $u $u))))
!(row template-fold)
!(template:foldl (1 2 3) 0 acc item @(let $s (+ acc item) $s))

; ---- Two instances of one code by lift let, and two openings by *.
!(row two-instances)
!(let $c @(let $u k $u) (Pair *(lift let k @1 $c) *(lift let k @2 $c)))
!(row two-openings)
!(let $c @(Pair $u k) (Pair *$c *$c))
!(row one-instance-opened-twice)
!(let $c @(Pair $u k) (let $i (lift let k @1 $c) (Pair *$i *$i)))

; ---- The quotation's own names are hidden: the code prints as written.
!(row printed)
!(let $c @(let $u k $u) $c)

; ---- An own name that a match binds is a name no longer: its value stands
; in its place, and an opening has nothing of it to copy.  A template's own
; name the same way.
!(row own-name-bound-by-a-match)
!(let $c @(Pair $u k) (unify $c (quote (Pair 1 k)) (Pair $c *$c) none))
!(row template-own-name-bound-by-a-match)
!(let $f (lam z (Pair z $y)) (unify $f (lam z (Pair z 5)) (Pair $f ($f 1)) none))

; ---- Negative controls.  A name written outside the quotation is not its
; own: every opening shares it.  A quotation built at run time owns nothing.
!(row shared-with-outside)
!(let $c @(Pair $w k) (Pair $w (Pair *$c *$c)))
!(row built-at-run-time)
!(let $c (cons-atom quote ((Pair $u k))) (Pair *$c *$c))
!(row lambda-in-code-owns-its-local)
!(let $c @(lam z (let $y z (g $y))) (let $f *$c (Pair ($f 1) ($f 2))))
