; A Prolog compound PeTTa holds, Predicate's value or one Prolog hands back,
; is a term, not a list: the reference's non_list/1 holds of it.  The total
; observers give (), the list relations have no answer or raise length/2's
; and msort/2's type error, and a cell onto it is an improper list.
!(let $c (Predicate (f a b)) (car-atom $c))
!(let $c (Predicate (f a b)) (cdr-atom $c))
!(let $c (Predicate (f a b)) (size-atom $c))
!(let $c (Predicate (f a b)) (sort-atom $c))
!(let $c (Predicate (f a b)) (min-atom $c))
!(let $c (Predicate (f a b)) (max-atom $c))
!(let $c (Predicate (f a b)) (unique-atom $c))
!(let $c (Predicate (f a b)) (alpha-unique-atom $c))
!(let $c (Predicate (f a b)) (collapse (index-atom $c 0)))
!(let $c (Predicate (f a b)) (collapse (decons-atom $c)))
!(let $c (Predicate (f a b)) (collapse (member $x $c)))
!(let $c (Predicate (f a b)) (collapse (superpose $c)))
!(let $c (Predicate (f a b)) (collapse (first $c)))
!(let $c (Predicate (f a b)) (collapse (exclude-item a $c)))
!(let $c (Predicate (f a b)) (collapse (get-metatype $c)))
!(let $c (Predicate (f a b)) (is-expr $c))
!(let $c (Predicate (f a b)) (cons-atom z $c))
!(let $c (Predicate (f a b)) (catch (length $c)))
!(let $c (Predicate (f a b)) (catch (msort $c)))
!(let (Error $f $c) (catch (sqrt-math -1.0)) (car-atom $f))

; Negative: a list keeps its elements.
!(car-atom (f a b))
!(size-atom (f a b))
!(is-expr (f a b))
