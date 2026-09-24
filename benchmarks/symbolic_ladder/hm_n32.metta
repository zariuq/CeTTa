; Hindley–Milner with real logical variables. Not MeTTa's type system.
; Polymorphic let re-infers the binding at each use. An ill-typed
; application answers nothing, and the following query still answers.
; Depth 32.

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
!(infer nil (app (lam x31 (var x31)) (app (lam x30 (var x30)) (app (lam x29 (var x29)) (app (lam x28 (var x28)) (app (lam x27 (var x27)) (app (lam x26 (var x26)) (app (lam x25 (var x25)) (app (lam x24 (var x24)) (app (lam x23 (var x23)) (app (lam x22 (var x22)) (app (lam x21 (var x21)) (app (lam x20 (var x20)) (app (lam x19 (var x19)) (app (lam x18 (var x18)) (app (lam x17 (var x17)) (app (lam x16 (var x16)) (app (lam x15 (var x15)) (app (lam x14 (var x14)) (app (lam x13 (var x13)) (app (lam x12 (var x12)) (app (lam x11 (var x11)) (app (lam x10 (var x10)) (app (lam x9 (var x9)) (app (lam x8 (var x8)) (app (lam x7 (var x7)) (app (lam x6 (var x6)) (app (lam x5 (var x5)) (app (lam x4 (var x4)) (app (lam x3 (var x3)) (app (lam x2 (var x2)) (app (lam x1 (var x1)) (app (lam x0 (var x0)) (li i))))))))))))))))))))))))))))))))))
!(infer nil (letv id (lam x (var x))
             (both (app (var id) (li i)) (app (var id) (li b)))))
!(let (fun $a $a) (infer nil (lam z (var z))) same)
!(let (fun int bool) (infer nil (lam z (var z))) bad)
!(infer nil (li b))
