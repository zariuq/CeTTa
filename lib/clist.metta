; Inductive presentation of finite Atom sequences.
;
; ClistNil and ClistCons present the same free monoid as list's expression
; representation. On well-formed clists:
;   to-list(from-list(xs)) = xs;
;   from-list(to-list(cs)) = cs;
; and the bridge preserves append, reverse, length, map, filter, and foldl.
; first/rest are intentionally defined only for ClistCons. Bulk traversal may
; use the expression-backed list lane without changing the denotation. Length
; and reverse force their next accumulators before tail-position continuation,
; so the eager and call-by-need lanes realize the same strict folds.

!(import! &self list)

(= (clist:nil)
   ClistNil)

(= (clist:cons $head $tail)
   (ClistCons $head $tail))

(= (clist:is-empty $list)
   (== $list ClistNil))

(= (clist:is-cons $list)
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

(= (clist:len $list)
   (clist:len-acc $list 0))

(= (clist:len-acc ClistNil $acc)
   $acc)

(= (clist:len-acc (ClistCons $head $tail) $acc)
   (let $next (eval (+ $acc 1))
     (eval (clist:len-acc $tail $next))))

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
   (let $next (eval (clist:cons $head $acc))
     (eval (clist:reverse-acc $tail $next))))

(= (clist:map $list $var $body)
   (clist:from-list
     (list:map (clist:to-list $list) $var $body)))

(= (clist:filter $list $var $body)
   (clist:from-list
     (list:filter (clist:to-list $list) $var $body)))

(= (clist:foldl $list $init $acc $item $step)
   (list:foldl (clist:to-list $list) $init $acc $item $step))

(= (clist:range $end)
   (clist:from-list (list:range $end)))

(= (clist:range $start $end)
   (clist:from-list (list:range $start $end)))

(= (clist:repeat $count $item)
   (clist:from-list (list:repeat $count $item)))
