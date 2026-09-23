; Hindley–Milner with real logical variables. Not MeTTa's type system.
; Polymorphic let re-infers the binding at each use. An ill-typed
; application answers nothing, and the following query still answers.
; Depth 16.

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

!(infer nil (app (li i) (li b)))
!(infer nil (app (lam x15 (var x15)) (app (lam x14 (var x14)) (app (lam x13 (var x13)) (app (lam x12 (var x12)) (app (lam x11 (var x11)) (app (lam x10 (var x10)) (app (lam x9 (var x9)) (app (lam x8 (var x8)) (app (lam x7 (var x7)) (app (lam x6 (var x6)) (app (lam x5 (var x5)) (app (lam x4 (var x4)) (app (lam x3 (var x3)) (app (lam x2 (var x2)) (app (lam x1 (var x1)) (app (lam x0 (var x0)) (li i))))))))))))))))))
!(infer nil (letv id (lam x (var x))
             (both (app (var id) (li i)) (app (var id) (li b)))))
!(let (fun $a $a) (infer nil (lam z (var z))) same)
!(let (fun int bool) (infer nil (lam z (var z))) bad)
!(infer nil (li b))
