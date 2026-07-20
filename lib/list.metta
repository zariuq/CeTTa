; Finite sequences represented by the children of an ordinary expression.
;
; Algebraic domain: list operations range over Expression atoms, identified
; with the free monoid of finite Atom sequences. On that domain:
;   append has () as identity and is associative;
;   reverse is an involution and preserves length;
;   cons/first/rest satisfy first(cons x xs)=x and rest(cons x xs)=xs.
; first, rest, at, and cons retain the native partiality of car-atom,
; cdr-atom, index-atom, and cons-atom outside their stated domains.
; Explicit inductive lists and the representation isomorphism live in clist.

(= (list:is-empty $xs)
   (== $xs ()))

(= (list:first $xs)
   (car-atom $xs))

(= (list:rest $xs)
   (cdr-atom $xs))

(= (list:len $xs)
   (size-atom $xs))

(= (list:at $xs $index)
   (index-atom $xs $index))

(= (list:cons $head $tail)
   (cons-atom $head $tail))

(= (list:push () $item)
   ($item))

(= (list:push $xs $item)
   (let ($head $tail) (decons-atom $xs)
     (let $rest (eval (list:push $tail $item))
       (cons-atom $head $rest))))

(= (list:append () $rhs)
   $rhs)

(= (list:append $lhs $rhs)
   (let ($head $tail) (decons-atom $lhs)
     (let $rest (eval (list:append $tail $rhs))
       (cons-atom $head $rest))))

(= (list:reverse $xs)
   (list:reverse-acc $xs ()))

(= (list:reverse-acc () $acc)
   $acc)

(= (list:reverse-acc $xs $acc)
   (let ($head $tail) (decons-atom $xs)
     (eval (list:reverse-acc $tail (cons-atom $head $acc)))))

(= (list:map $xs $var $body)
   (map-atom $xs $var $body))

(= (list:filter $xs $var $body)
   (filter-atom $xs $var $body))

(= (list:foldl $xs $init $acc $item $step)
   (foldl-atom $xs $init $acc $item $step))

(= (list:range $end)
   (range-atom $end))

(= (list:range $start $end)
   (range-atom $start $end))

(= (list:repeat $count $item)
   (repeat-atom $count $item))
