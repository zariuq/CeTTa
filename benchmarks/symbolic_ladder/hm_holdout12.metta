; Hindley–Milner holdout. Same rules as the depth ladder, different terms.
; Bool is instantiated before int. An open identity is refined through an
; inner identity. A domain/range clash answers nothing, and the next query
; still answers.

(= (lookup ((pair $n $t) $rest) $n) $t)
(= (lookup ((pair $m $t) $rest) $n) (lookup $rest $n))
(= (lookup ((poly $n $e $env0) $rest) $n) (infer $env0 $e))
(= (lookup ((poly $m $e $env0) $rest) $n) (lookup $rest $n))

(= (infer $env (var $n)) (lookup $env $n))
(= (infer $env (li i)) int)
(= (infer $env (li b)) bool)
(= (infer $env (lam $x $body))
   (let $bt (infer ((pair $x $a) $env) $body) (fun $a $bt)))
(= (infer $env (app $f $arg))
   (let (fun $d $r) (infer $env $f)
     (let $at (infer $env $arg)
       (let $_ (uni $d $at) $r))))
(= (infer $env (letv $x $e $body))
   (infer ((poly $x $e $env) $env) $body))
(= (infer $env (both $p $q))
   (let $tp (infer $env $p)
     (let $tq (infer $env $q) (prod $tp $tq))))
(= (uni $x $x) ok)

!(infer nil (app (li b) (li i)))
!(infer nil (app (lam f (app (var f) (li b))) (lam y (var y))))
!(infer nil (letv id (lam x (var x))
             (both (app (var id) (li b)) (app (var id) (li i)))))
!(let (fun $a $a) (infer nil (lam z (app (lam w (var w)) (var z)))) twin)
!(let (fun int bool) (infer nil (lam z (var z))) clash)
!(infer nil (both (li b) (li b)))
