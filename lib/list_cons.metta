; Explicit cons-list helpers.
;
; Design intent:
; - Keep a semantic inductive list lane separate from ordinary expressions.
; - Use this when head/tail structure matters logically.
; - Convert to expression-backed sequences for bulk traversal when convenient.

!(import! &self list_expr)

(= (conslist:nil)
   ConsListNil)

(= (conslist:cons $head $tail)
   (ConsListCons $head $tail))

(= (conslist:nil? $list)
   (== $list ConsListNil))

(= (conslist:cons? $list)
   (case $list
     (((ConsListCons $head $tail) True)
      ($else False))))

(= (conslist:head (ConsListCons $head $tail))
   $head)

(= (conslist:tail (ConsListCons $head $tail))
   $tail)

(= (conslist:from-expr ())
   (conslist:nil))

(= (conslist:from-expr $xs)
   (let ($head $tail) (decons-atom $xs)
     (let $rest (eval (conslist:from-expr $tail))
       (conslist:cons $head $rest))))

(= (conslist:to-expr ConsListNil)
   ())

(= (conslist:to-expr (ConsListCons $head $tail))
   (let $rest (eval (conslist:to-expr $tail))
     (cons-atom $head $rest)))

(= (conslist:length ConsListNil)
   0)

(= (conslist:length (ConsListCons $head $tail))
   (+ 1 (conslist:length $tail)))

(= (conslist:append ConsListNil $rhs)
   $rhs)

(= (conslist:append (ConsListCons $head $tail) $rhs)
   (let $rest (eval (conslist:append $tail $rhs))
     (conslist:cons $head $rest)))

(= (conslist:reverse $list)
   (conslist:reverse-acc $list (conslist:nil)))

(= (conslist:reverse-acc ConsListNil $acc)
   $acc)

(= (conslist:reverse-acc (ConsListCons $head $tail) $acc)
   (eval (conslist:reverse-acc $tail (conslist:cons $head $acc))))

(= (conslist:map $list $var $body)
   (conslist:from-expr
     (expr-map (conslist:to-expr $list) $var $body)))

(= (conslist:filter $list $var $body)
   (conslist:from-expr
     (expr-filter (conslist:to-expr $list) $var $body)))

(= (conslist:foldl $list $init $acc $item $step)
   (expr-foldl (conslist:to-expr $list) $init $acc $item $step))
