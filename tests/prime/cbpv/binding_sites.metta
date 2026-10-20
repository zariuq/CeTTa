; The binding-site table of lib/prime/cbpv.metta, read against the draft.
; The table gives each binding form a policy (what a name written in its
; pattern position does) and a strategy (how the value bound there is
; computed).  Each row observes the draft at one site, lists what each
; policy or strategy would give there, and checks the observation against
; the entry the table selects; the predictions are quoted, so that none of
; them runs.  A change on either side shows as False.  The lambda's strategy
; is tested in lambda_argument_shared.metta.
!(import! &self ../../../lib/prime/cbpv.metta)

(site-fact 2)
(= (pick) heads)
(= (pick) tails)
(= (ignore $x) done)
(= (pp $x) (P $x $x))

(= (lookup $key $pairs)
   (if (== $pairs ()) unlisted
       (let ($k $v) (car-atom $pairs)
         (if (== $k $key) $v (let $rest (cdr-atom $pairs) (lookup $key $rest))))))
(= (policy $head $observed $quoted)
   (let (quote $predictions) $quoted
     (let $p (cbpv:site-policy $head)
       (let $predicted (lookup $p $predictions)
         (Policy $head $p (== $observed $predicted))))))
(= (strategy $head $observed $quoted)
   (let (quote $predictions) $quoted
   (let $s (cbpv:site-strategy $head)
     (let $predicted (lookup $s $predictions)
       (let $mine (sort-atom $predicted)
         (let $theirs (sort-atom $observed)
           (Strategy $head $s (== $theirs $mine))))))))
(= (crossing $head $observed $refers) (Crossing $head (== $observed $refers)))

; Policies.  The enclosing $x is 1 and the site binds 2: a fresh or lexical
; name binds anew, so the body sees 2; a referring name is the enclosing 1,
; so the site does not match; a sealed quotation keeps its own variable.
!(policy let (collapse (let $x 1 (let $x 2 $x))) (quote ((fresh (2)) (refer ()))))
!(policy let* (collapse (let $x 1 (let* (($x 2)) $x))) (quote ((fresh (2)) (refer ()))))
!(policy case (collapse (let $x 1 (case 2 (($x $x))))) (quote ((fresh (2)) (refer ()))))
!(policy switch (collapse (let $x 1 (switch 2 (($x $x))))) (quote ((fresh (2)) (refer ()))))
!(policy switch-minimal (collapse (let $x 1 (switch-minimal 2 (($x $x))))) (quote ((fresh (2)) (refer ()))))
!(policy match (collapse (let $x 1 (match &self (site-fact $x) $x))) (quote ((fresh (2)) (refer ()))))
!(policy chain (collapse (let $x 1 (chain 2 $x $x))) (quote ((fresh (2)) (refer ()))))
!(policy filter-atom (collapse (let $x 1 (filter-atom (2) $x True))) (quote ((fresh ((2))) (refer (())))))
!(policy unify (collapse (let $x 1 (unify $x 2 $x no))) (quote ((fresh (2)) (refer (no)))))
!(policy = (collapse (let $x 5 (let $e (= (double $x) 0)
                       (let $d (index-atom $e 1) (let $a (index-atom $d 1) (get-metatype $a))))))
   (quote ((fresh (Variable)) (refer (Grounded)))))
!(policy lam (collapse (let $x 1 ((lam $x $x) 2))) (quote ((lexical (2)) (refer ()))))
!(policy map-atom (collapse (let $x 1 (map-atom (2) $x $x))) (quote ((lexical ((2))) (refer ((1))))))
!(policy foldl-atom (collapse (let $x 1 (foldl-atom (2) 0 $acc $x (+ $acc $x)))) (quote ((lexical (2)) (refer (1)))))
!(policy new (collapse (let $x 1 (new ($x) (get-metatype $x)))) (quote ((lexical (Variable)) (refer (Grounded)))))
!(policy quote (collapse (let $x 1 (let (quote $c) (quote $x) (get-metatype $c))))
   (quote ((sealed (Variable)) (refer (Grounded)))))

; Crossing sets: F{$x} makes $x refer to the enclosing $x; on a quotation it
; lifts the value in.
!(crossing let (collapse (let $x 1 (let $x 2 $x){$x})) ())
!(crossing case (collapse (let $x 1 (case 2 (($x $x))){$x})) ())
!(crossing quote (collapse (let $x 1 (let (quote $c) (quote $x){$x} (get-metatype $c)))) (Grounded))

; Strategies.  A value is computed before the scope, once; need at the first
; demand, shared; data is not evaluated; a template runs once per element.
!(strategy let (collapse (let $c (pick) done)) (quote ((value (done done)) (need (done)))))
!(strategy let* (collapse (let* (($c (pick))) done)) (quote ((value (done done)) (need (done)))))
!(strategy case (collapse (case (pick) (($c done)))) (quote ((value (done done)) (need (done)))))
!(strategy chain (collapse (chain (pick) $c done)) (quote ((value (done done)) (need (done)))))
!(strategy = (collapse (ignore (pick))) (quote ((value (done done)) (need (done)))))
!(strategy = (collapse (pp (pick)))
   (quote ((need ((P heads heads) (P tails tails)))
    (name ((P heads heads) (P heads tails) (P tails heads) (P tails tails))))))
!(strategy unify (collapse (unify (pick) (pick) same differ))
   (quote ((value (same differ differ same)) (data (same)))))
!(strategy map-atom (collapse (map-atom (1 2) $x (pick)))
   (quote ((value ((heads heads) (tails tails)))
    (template ((heads heads) (heads tails) (tails heads) (tails tails))))))
!(strategy quote (collapse (quote (pick))) (quote ((value ((quote heads) (quote tails))) (data ((quote (pick)))))))
