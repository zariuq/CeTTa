; Explicit cons-list helpers.
;
; Design intent:
; - This is the semantic/inductive list lane.
; - Use it when constructors and head/tail structure matter logically.
; - Bulk traversal can route through the fast list lane when appropriate.

!(import! &self list)

(= (clist:nil)
   ClistNil)

(= (clist:cons $head $tail)
   (ClistCons $head $tail))

(= (clist:empty? $list)
   (== $list ClistNil))

(= (clist:cons? $list)
   (case $list
     (((ClistCons $head $tail) True)
      ($else False))))

(= (clist:first (ClistCons $head $tail))
   $head)

(= (clist:rest (ClistCons $head $tail))
   $tail)

(= (clist:from-list ())
   (clist:nil))

(= (clist:from-list $xs)
   (let ($head $tail) (decons-atom $xs)
     (let $rest (eval (clist:from-list $tail))
       (clist:cons $head $rest))))

(= (clist:to-list ClistNil)
   ())

(= (clist:to-list (ClistCons $head $tail))
   (let $rest (eval (clist:to-list $tail))
     (cons-atom $head $rest)))

(= (clist:len ClistNil)
   0)

(= (clist:len (ClistCons $head $tail))
   (+ 1 (clist:len $tail)))

(= (clist:append ClistNil $rhs)
   $rhs)

(= (clist:append (ClistCons $head $tail) $rhs)
   (let $rest (eval (clist:append $tail $rhs))
     (clist:cons $head $rest)))

(= (clist:reverse $list)
   (clist:reverse-acc $list (clist:nil)))

(= (clist:reverse-acc ClistNil $acc)
   $acc)

(= (clist:reverse-acc (ClistCons $head $tail) $acc)
   (eval (clist:reverse-acc $tail (clist:cons $head $acc))))

(= (clist:map $list $var $body)
   (clist:from-list
     (list:map (clist:to-list $list) $var $body)))

(= (clist:filter $list $var $body)
   (clist:from-list
     (list:filter (clist:to-list $list) $var $body)))

(= (clist:foldl $list $init $acc $item $step)
   (list:foldl (clist:to-list $list) $init $acc $item $step))
