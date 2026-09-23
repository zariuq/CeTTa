; Monomorphic inference with logical variables. Not MeTTa's own type system.
; A following query still answers after an ill-typed application fails.

(ann x int)
(ann y bool)

(= (infer (var $name) $type) (match &self (ann $name $type) $type))
(= (infer (lam $body) (fun $domain $result)) (infer $body $result))
(= (infer (app $fun $arg) $result)
   (let $funtype (infer $fun (fun $domain $result))
     (let $_ (infer $arg $domain) $result)))

!(infer (var x) $t)
!(infer (app (var x) (var y)) $t)
!(infer (app (lam (var y)) (var x)) $t)
