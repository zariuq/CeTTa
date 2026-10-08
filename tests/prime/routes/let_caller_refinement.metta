; Implicit caller and sibling refinement under rule M.
(scope:profile mercury-implicit per-call reference)

; A `$` name is a store name of its whole form.  The `let` that mentions it
; first binds it, and every other mention sees the binding: the caller's
; variable after an equation call, a sibling `let`, and the rest of the form.
; Matching refines a name; it never shadows it.  This file is run with and
; without --fuel; the answers must be the same.

(= (pair-of-ab) (d a b))
(: s1 srt)
(: s2 srt)
(: sym1 (prim s1 s2))
(= (decl $bias $prims)
   (match $bias (: $symbol ($prims $src $tgt)) (hdecl $src $tgt $symbol)))

; ---- A let pattern refines the caller's variable through an equation call.
(= (second $s $t) (let (d $s $t) (pair-of-ab) (Got $t)))
!(let $r (second a $t) (Pair $t $r))
; The same when the source is a query of the space.
(= (signature $s $t)
   (let (hdecl $s $t $sym) (decl &self prim) (Got $s $t $sym)))
!(let $r (signature s1 $x) (Pair $x $r))
; Whatever the body holds, a tuple headed by an expression included.
(= (tuple-body $s $t)
   (let (hdecl $s $t $sym) (decl &self prim) (Got ((f x)))))
!(let $r (tuple-body s1 $x) (Pair $x $r))
; A pattern that is a single variable refines too.
(= (to-five $x) (let $x 5 ok))
!(let $r (to-five $y) (Pair $y $r))
; What runs after the call sees the refined value.
!(let $r (second a $t) (if (== $t b) (Pair $t yes) no))
!(let $r (second a $t) (if (== $t c) (Pair $t yes) no))

; ---- Within one form, a name a let binds is seen after the let.
!(let $q (let (d $y) (d 5) ok) (Pair $y $q))

; ---- Siblings share their names: two values for one name give no answer;
; the same value twice agrees.
!(collapse (Pair (let $x 1 $x) (let $x 2 $x)))
!(Pair (let $x 1 $x) (let $x 1 $x))

; ---- A name the pattern does not mention is not refined.  An equation's
; names are renamed apart at each call, so a caller's variable spelled like
; one of them is a different name.
(= (third $s $t) (let (d $s $u) (pair-of-ab) (Got $u)))
!(let $r (third a $t) (Pair $t $r))
(= (own $t) (let (d $s $u) (pair-of-ab) (Got $s $u)))
!(let $r (own $u) (Pair $u $r))

; ---- A caller's value the pattern disagrees with removes the branch.
!(collapse (let $t c (second a $t)))

; ---- A name the pattern holds inside quoted code or a held value is
; syntax, not a store name: the let opens it on its own, and the code it
; came from keeps its binder, so the code can be opened again at another
; argument.
(= (open-code $code $arg)
   (unify $code (quote (lam $x $body)) (let (quote $x) $arg @$body) (empty)))
!(let $h @(lam $v (f $v $v)) (Pair (open-code $h @a) (open-code $h @b)))
(: open-held (-> (Data Atom) (Data Atom) (Data Atom)))
(= (open-held (lam $x $body) $v) (let $x $v $body))
!(let $h (lam $y (and (f $y) (g $y)))
   (pair (open-held $h 0) (pair (open-held $h 1) $h)))
