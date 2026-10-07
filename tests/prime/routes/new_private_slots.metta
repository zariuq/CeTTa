; `(new ($h ...) body)`: fresh private slots, one set per evaluation of the
; form.  Inside a lambda body that is once per activation.  The names are
; lexical binders of the body, as a lambda's parameters are: owned by the
; `new`, hidden from the caller, and never captured by spelling.  The
; profile here is the default, rule M; new_private_slots_lexical_fresh.metta
; has the translated rows under lexical-fresh.
(= (mk-new) (lam z (new ($w) (Pair z $w))))

; ---- One slot per activation.
!(row per-activation)
!(let $f (lam z (new ($hole) $hole)) (Pair ($f 1) ($f 2)))
!(row per-activation-refined)
!(let $f (lam z (new ($hole) $hole)) (let $a ($f 1) (let $b ($f 2) (unify $a Left (unify $b Right (Pair $a $b) (empty)) (empty)))))
; One evaluation, one slot, used twice; two evaluations, two slots.
!(row once-per-evaluation)
!(new ($h) (Pair $h $h))
!(row two-evaluations)
!(Pair (new ($h) $h) (new ($h) $h))
; A pattern in the body refines the slot.
!(row refined-inside)
!(new ($h) (let $h 5 (Pair $h $h)))

; ---- Hidden from the caller, never captured by spelling.
!(row caller-name-apart)
!(let $f (lam z (new ($h) (Pair z $h))) (Pair ($f 1) $h))
; The library's slot and the query's name are both spelled $w.
!(row cross-form)
!(let $w 5 (Pair ((mk-new) 1) $w))
; An argument that mentions the caller's $h is inserted under the slot's
; binder, which never captures it.
!(row argument-not-captured)
!((lam v (new ($h) (Pair v $h))) $h)

; ---- Lifetime: a `new` is per evaluation under per-closure too.
!(scope:profile mercury-implicit per-closure)
!(row per-closure)
!(let $f (lam z (new ($hole) $hole)) (Pair ($f 1) ($f 2)))
!(scope:profile mercury-implicit)

; ---- Code: a `new` in quoted code is syntax until the code is opened.
!(row in-code)
!(let $c @(new ($h) (Pair $h $h)) (Pair $c *$c))
!(row quoted-name-literal)
!(new ($h) @$h)

; ---- Any other shape is data.
!(row not-a-list-of-variables)
!(new (a b) c)
!(new $x c)
!(new 1)
