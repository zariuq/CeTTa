; Expression-backed sequence helpers.
;
; Design intent:
; - Use ordinary MeTTa expressions as the default "fast list" lane.
; - Keep this layer thin over CeTTa native expression primitives.
; - Reserve explicit cons-lists for inductive semantics, not hot-path state.

(= (expr-empty? $xs)
   (== $xs ()))

(= (expr-head $xs)
   (car-atom $xs))

(= (expr-tail $xs)
   (cdr-atom $xs))

(= (expr-length $xs)
   (size-atom $xs))

(= (expr-get $xs $index)
   (index-atom $xs $index))

(= (expr-cons $head $tail)
   (cons-atom $head $tail))

(= (expr-snoc () $item)
   ($item))

(= (expr-snoc $xs $item)
   (let ($head $tail) (decons-atom $xs)
     (let $rest (eval (expr-snoc $tail $item))
       (cons-atom $head $rest))))

(= (expr-append () $rhs)
   $rhs)

(= (expr-append $lhs $rhs)
   (let ($head $tail) (decons-atom $lhs)
     (let $rest (eval (expr-append $tail $rhs))
       (cons-atom $head $rest))))

(= (expr-reverse $xs)
   (expr-reverse-acc $xs ()))

(= (expr-reverse-acc () $acc)
   $acc)

(= (expr-reverse-acc $xs $acc)
   (let ($head $tail) (decons-atom $xs)
     (eval (expr-reverse-acc $tail (cons-atom $head $acc)))))

(= (expr-map $xs $var $body)
   (map-atom $xs $var $body))

(= (expr-filter $xs $var $body)
   (filter-atom $xs $var $body))

(= (expr-foldl $xs $init $acc $item $step)
   (foldl-atom $xs $init $acc $item $step))
