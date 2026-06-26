; Regression: a ground query evaluated through `let`/bind mode must not preserve
; internal rule variables as caller-visible bindings.  Preserving those internal
; bindings made nested recursive same-head equations disappear in default CeTTa,
; even though the same program evaluates in LeaTTa, Hyperon, and PeTTa.

(= (shift $d $c (Var $k)) (if (< $k $c) (Var $k) (Var (+ $k $d))))
(= (shift $d $c (Srt $s)) (Srt $s))
(= (shift $d $c (Pi $A $B)) (Pi (shift $d $c $A) (shift $d (+ $c 1) $B)))
(= (shift $d $c (Bad $e)) (Bad $e))

(= (nf (Var $k)) (Var $k))
(= (nf (Srt $s)) (Srt $s))
(= (nf (Pi $A $B)) (Pi (nf $A) (nf $B)))
(= (nf (Bad $e)) (Bad $e))

(= (ctx-nth $i CtxNil) (Bad var-out-of-range))
(= (ctx-nth $i (CtxCons $h $t)) (if (== $i 0) $h (ctx-nth (- $i 1) $t)))
(= (ctx-type $i $ctx) (shift (+ $i 1) 0 (ctx-nth $i $ctx)))

(= (pts-rule $sA $sB)
   (unify $sA (Srt type)
     (unify $sB (Srt type) (Srt type)
       (unify $sB (Srt kind) (Srt kind) (Bad pi-bad-codomain)))
     (unify $sA (Srt kind)
       (unify $sB (Srt type) (Srt kind)
         (unify $sB (Srt kind) (Srt kind) (Bad pi-bad-codomain)))
       (Bad pi-domain-not-type))))

(= (infer $ctx (Srt type)) (Srt kind))
(= (infer $ctx (Srt kind)) (Bad kind-is-topsort))
(= (infer $ctx (Bad $e)) (Bad $e))
(= (infer $ctx (Var $i)) (ctx-type $i $ctx))
(= (infer $ctx (Pi $A $B))
   (let $sA (nf (infer $ctx $A))
     (let $sB (nf (infer (CtxCons $A $ctx) $B))
       (pts-rule $sA $sB))))

(= (id-ty-expanded)
   (Pi (Srt type) (Pi (Var 0) (Pi (Var 1) (Srt type)))))

!(assertEqual (let $x (infer CtxNil (id-ty-expanded)) (nf $x)) (Srt kind))
!(assertEqual (nf (infer CtxNil (id-ty-expanded))) (Srt kind))
