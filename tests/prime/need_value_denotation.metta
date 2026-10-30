; A Need cell denotes the value of its source.  Passing an argument lazily
; never changes what a program stores, compares or holds: the answers here are
; the eager answers.  Effects run once, at their first demand, and an argument
; that is never demanded never runs.

!(bind! &nvd:s (new-space))
(= (nvd:noisy $v) (let $_ (println! (nvd:ran $v)) $v))

; ---- storage: a space holds values
(= (nvd:put $x) (let $_ (add-atom &nvd:s (put $x)) ok))
!(nvd:put (+ 2 2))
!(collapse (match &nvd:s (put $v) $v))
!(collapse (match &nvd:s (put 4) yes))
; removal and nodup meet the stored value
!(add-atom &nvd:s (item 3))
(= (nvd:rm $x) (let $_ (remove-atom &nvd:s (item $x)) (removed $x)))
!(nvd:rm (+ 1 2))
!(collapse (match &nvd:s (item $v) $v))
(= (nvd:nodup $x) (add-atom-nodup &nvd:s (once $x)))
!(nvd:nodup (+ 1 1))
!(nvd:nodup 2)
!(collapse (match &nvd:s (once $v) $v))
; a stored effect runs once, at storage, and a later read runs nothing
(= (nvd:store-noisy $x) (let $_ (add-atom &nvd:s (logged $x)) stored))
!(nvd:store-noisy (nvd:noisy 5))
!(collapse (match &nvd:s (logged $v) $v))
!(collapse (match &nvd:s (logged $v) $v))
; one store per value of a nondeterministic argument, as eagerly
(= (nvd:choice $x) (add-atom &nvd:s (choice $x)))
!(nvd:choice (superpose (A B)))
!(sort-atom (collapse (match &nvd:s (choice $v) $v)))
; written syntax in the payload stays data; an explicit suspension stays one
(= (nvd:written $x) (add-atom &nvd:s (written (+ 1 1) $x)))
!(nvd:written (+ 2 3))
!(collapse (match &nvd:s (written $a $b) ($a $b)))
(= (nvd:deferred $x) (add-atom &nvd:s (deferred (delay (nvd:noisy $x)))))
!(nvd:deferred 9)
!(collapse (match &nvd:s (deferred $p) $p))
; a pending field of a value earned earlier is demanded too
(= (nvd:box $x) (Box $x))
(= (nvd:store-box $b) (let $_ (== $b $b) (add-atom &nvd:s (boxed $b))))
!(nvd:store-box (nvd:box (+ 3 4)))
!(collapse (match &nvd:s (boxed (Box 7)) yes))

; ---- the bank: no money is made
!(bind! &nvd:acct (new-space))
!(add-atom &nvd:acct (bal 1 1000))
!(add-atom &nvd:acct (bal 2 1000))
(= (nvd:transfer $from $to $n)
   (let $as (collapse (match &nvd:acct (bal $w $a) (if (== $w $from) $a (empty))))
     (let $a (car-atom $as)
       (let $_ (remove-atom &nvd:acct (bal $from $a))
         (let $a2 (- $a $n)
           (let $_2 (add-atom &nvd:acct (bal $from $a2))
             (let $bs (collapse (match &nvd:acct (bal $w $b) (if (== $w $to) $b (empty))))
               (let $b (car-atom $bs)
                 (let $_3 (remove-atom &nvd:acct (bal $to $b))
                   (let $b2 (+ $b $n) (add-atom &nvd:acct (bal $to $b2))))))))))))
!(nvd:transfer (+ 0 1) (+ 1 1) 10)
!(sort-atom (collapse (match &nvd:acct (bal $w $v) (bal $w $v))))
!(let $vs (collapse (match &nvd:acct (bal $w $v) $v)) (foldl-atom $vs 0 $acc $x (+ $acc $x)))

; ---- held positions hold the cell, never its source
(: nvd:hold (-> Atom Proc))
(= (nvd:hold $d) (held $d))
(= (nvd:pass $x) (nvd:hold $x))
!(nvd:pass (+ 1 2))
; held twice is computed once: two answers, not four, and one effect
(: nvd:both (-> Atom Atom Proc))
(= (nvd:both $a $b) (pair (eval $a) (eval $b)))
(= (nvd:twice $x) (nvd:both $x $x))
!(sort-atom (collapse (nvd:twice (superpose (1 2)))))
!(nvd:twice (nvd:noisy 6))
; a variable the argument binds itself does not make it code
!(nvd:twice (let $_ (add-atom &nvd:s tock) 7))
!(collapse (match &nvd:s tock tock))
; readers of a held cell read its value
(: nvd:head (-> Atom Proc))
(= (nvd:head $x) (car-atom $x))
(: nvd:size (-> Atom Proc))
(= (nvd:size $x) (size-atom $x))
(: nvd:same (-> Atom Proc))
(= (nvd:same $x) (== $x (Box 1)))
(= (nvd:mk1) (Box 1))
(= (nvd:mk2) (Box 1 2))
(= (nvd:read-head $y) (nvd:head $y))
(= (nvd:read-size $y) (nvd:size $y))
(= (nvd:read-same $y) (nvd:same $y))
!(nvd:read-head (nvd:mk1))
!(nvd:read-size (nvd:mk2))
!(nvd:read-same (nvd:mk1))
; an equation head reads a held cell's value; written syntax stays syntax
(: nvd:inspect (-> Atom Proc))
(= (nvd:inspect (+ $a $b)) (syntax-plus $a $b))
(= (nvd:inspect $other) (value $other))
(= (nvd:pass-inspect $x) (nvd:inspect $x))
!(nvd:pass-inspect (+ 1 2))
!(sort-atom (collapse (nvd:inspect (+ 1 2))))
; a value produced earlier and bound by let is held as that value
(: nvd:seen (-> Atom Proc))
(= (nvd:seen $l) (Seen $l))
(= (nvd:succ-of $n) (Next $n))
!(let $l (cdr-atom (x nvd:succ-of 5)) (nvd:seen $l))
; a library template is written syntax: it runs once per element
(= (nvd:each $items) (map-atom $items $e (println! (nvd:item $e))))
!(nvd:each ((I 1) (I 2)))
; the code of a value is the whole value, so standardizing apart renames it
(= (nvd:apart $l $r)
   (let $lc (lift $l)
     (let $rc (lift $r)
       (let (quote $fl) (sealed () $lc)
         (let (quote $fr) (sealed () $rc)
           (let (In (($k $la))) $fl
             (let (In (($m $lb))) $fr
               (unify $la $lb unified apart))))))))
!(nvd:apart (In ((P (p $x)))) (In ((N (p (f $x))))))

; a template is code, not a value: an argument whose free variable its
; consumer binds is passed on as written, through an untyped wrapper too
!(import! &self list)
!(list:filter (1 2 3) $x (> $x 1))

; ---- laziness stays
(= (nvd:loop) (nvd:loop))
(= (nvd:my-if $c $t $e) (if $c $t $e))
!(nvd:my-if True 1 (nvd:loop))
!(nvd:my-if False (nvd:loop) 2)
(= (nvd:unused $x) ignored)
!(nvd:unused (nvd:noisy 8))
(: nvd:ignore-held (-> Atom Proc))
(= (nvd:ignore-held $d) ignored)
(= (nvd:pass-ignored $x) (nvd:ignore-held $x))
!(nvd:pass-ignored (nvd:noisy 10))

; ---- negative control: an explicit resampling region still resamples
!(let $r (resample (superpose (A B))) (sort-atom (collapse (Pair (force $r) (force $r)))))
(= (nvd:resampled $r) (Pair (force $r) (force $r)))
!(sort-atom (collapse (nvd:resampled (resample (superpose (A B))))))
