; Lifted syntax operations on code values: an equation-level PROTOTYPE.
;
; Prime has no native `lift` yet and its reader has no `C[x := V]` brackets;
; the two operations below are ordinary equations over `Data`.
;   (lift app F A)   forms the application code @(f a) from code values.
;   (lift let k A C) substitutes the code A for the slot k in the code C.
; Both demand code values and stay as written otherwise.  Each acts on one
; level of code (a nested quotation is sealed) and nothing runs.  The
; prototype is first-order and NOT capture-avoiding: it does not know
; binders, so its output is never hygienic (see the negative controls at the
; end; native lambda substitution is binder-aware).  Hygiene is required of
; the native implementation.

(= (observe $v) $v)

; Structural replacement of one slot, held throughout; nested quotes sealed.
(: replace (-> (Data Atom) (Data Atom) (Data Atom) (Data Atom)))
(= (replace $s $v $t)
   (if (noreduce-eq $t $s)
       $v
       (if (== (get-metatype $t) Expression)
           (replace-children $s $v $t)
           $t)))
(: replace-children (-> (Data Atom) (Data Atom) (Data Atom) (Data Atom)))
(= (replace-children $s $v $t)
   (if (noreduce-eq $t ())
       ()
       (let ($h $rest) (decons-atom $t)
         (if (noreduce-eq $h quote)
             $t
             (let $h2 (replace $s $v $h)
               (let $rest2 (replace-children $s $v $rest)
                 (cons-atom $h2 $rest2)))))))

(= (lift app (quote $f) (quote $a)) (quote ($f $a)))
(= (lift let $s (quote $a) (quote $c))
   (let $r (replace $s $a $c) (quote $r)))

; Simultaneous substitution of two slots: rename both slots to fresh
; variables first (equation variables are fresh at each call), then fill.
(= (lift let2 $s1 $s2 (quote $a1) (quote $a2) (quote $c))
   (let $r1 (replace $s1 $f1 $c)
     (let $r2 (replace $s2 $f2 $r1)
       (let $r3 (replace $f1 $a1 $r2)
         (let $r4 (replace $f2 $a2 $r3)
           (quote $r4))))))

; --- application formation ---------------------------------------------------
; 1-2. Builds syntax; the operand's effect does not run.
!(observe (lift app @foo @a))
!(observe (lift app @foo @(println! "must-not-run")))
; 3-4. Forming an application is not beta: the redex is kept, and only its
;      activation reduces it.
!(observe (lift app @(lam x x) @a))
!(let $c (lift app @(lam x x) @a) *$c)
; 5. Argument discipline: an operand that is not a code value stays as written.
!(lift app @foo a)

; --- substitution into code --------------------------------------------------
; 6-7. A symbol slot and a variable slot.
!(observe (lift let x @a @(foo x)))
!(observe (lift let $x @a @(foo $x)))
; 8. The inserted code is inserted as written, not evaluated.
!(observe (lift let x @(+ 1 2) @(foo x)))
; 9. One level: a nested quotation is sealed.
!(observe (lift let x @a @(foo x @x)))
; 10. A slot that does not occur leaves the code unchanged.
!(observe (lift let y @a @(foo x)))
; 11. Argument discipline: an unquoted replacement stays as written.
!(lift let x a @(foo x))
; 12-13. Composition: C[x := (g y)][y := b] equals C[x := (g b), y := b].
!(observe (lift let y @b (lift let x @(g y) @(f x y))))
!(observe (lift let x @(g b) (lift let y @b @(f x y))))
; 14-15. Sequential and simultaneous differ: [x := y, y := a] on (pair x y).
!(observe (lift let y @a (lift let x @y @(pair x y))))
!(observe (lift let2 x y @y @a @(pair x y)))
; 16. The code is data until activated, and runs when activated.
!(let $c (lift let x @(+ 1 2) @(* x 10)) (pair $c *$c))

; --- binders -----------------------------------------------------------------
; 17. NEGATIVE CONTROL: the first-order prototype wrongly substitutes a bound y.
!(observe (lift let y @w @(lam y (pair y z))))
; 17b. NEGATIVE CONTROL, capture: inserting y under a binder y captures it.
;      The result is (lam y (foo y y)); a hygienic substitution gives a code
;      alpha-equivalent to (lam z (foo y z)).
!(observe (lift let x @y @(lam y (foo x y))))
; 17c. NEGATIVE CONTROL, shadowing: the inner binder x shadows the slot, so
;      a hygienic substitution leaves (lam x (foo x)) unchanged.
!(observe (lift let x @a @(lam x (foo x))))
; 18. Native lambda substitution avoids capture: the inner binder is renamed.
!(observe ((lam x (lam y (pair x y))) y))

; --- the proposed bracket spelling is not in the reader ----------------------
; 19. Brackets fuse into neighbouring tokens today.
!(parse "(f @(foo $x)[$x := @a])")
; 20. A bare bracketed template is two forms, which the one-form reader
;     refuses.  (A top-level error ends the run, so this query is last.)
!(parse "@(foo $x)[$x := @a]")
