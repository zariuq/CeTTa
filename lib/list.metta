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

(= (list:push $xs $item)
   (union-atom $xs ($item)))

(= (list:append $lhs $rhs)
   (union-atom $lhs $rhs))

(= (list:reverse $xs)
   (list:reverse-acc $xs ()))

(= (list:reverse-acc () $acc)
   $acc)

(= (list:reverse-acc $xs $acc)
   (let ($head $tail) (decons-atom $xs)
     (let $next (eval (cons-atom $head $acc))
       (eval (list:reverse-acc $tail $next)))))

; Structural equality is the default equivalence for list filtering.  The
; explicit predicate argument on the -by forms also admits alpha, key-only,
; or domain-specific equivalences without changing the list representation.
(= (list:atom-equal $left $right)
   (== $left $right))

(= (list:remove-all-by $same $item $xs)
   (if (== $xs ())
       ()
       (let ($head $tail) (decons-atom $xs)
         (let $drop (eval ($same $item $head))
           (if $drop
               (eval (list:remove-all-by $same $item $tail))
               (let $rest
                    (eval (list:remove-all-by $same $item $tail))
                 (eval (cons-atom $head $rest))))))))

(= (list:remove-all $item $xs)
   (remove-all-atom $item $xs))

; unique-atom is the linear expected-time fast path.  It preserves the first
; occurrence and uses structural equality on closed atoms; open atoms are
; identified modulo alpha-renaming.  unique-by exposes other equivalences.
(= (list:unique $xs)
   (unique-atom $xs))

(= (list:unique-by $same $xs)
   (if (== $xs ())
       ()
       (let ($head $tail) (decons-atom $xs)
         (let $remaining
              (eval (list:remove-all-by $same $head $tail))
           (let $rest (eval (list:unique-by $same $remaining))
             (eval (cons-atom $head $rest)))))))

; The prefix action of the natural numbers on finite sequences.  A request
; beyond the sequence length returns the whole sequence; a non-positive
; request returns the empty prefix.
(= (list:take $count $xs)
   (if (<= $count 0)
       ()
       (if (== $xs ())
           ()
           (let ($head $tail) (decons-atom $xs)
             (let $rest (eval (list:take (- $count 1) $tail))
               (eval (cons-atom $head $rest)))))))

; Efficient order-statistic filtering for already-computed numeric keys.
; Each child is `(key payload)`.  Selection is by descending key, but output
; retains the input occurrence order; earlier occurrences win boundary ties.
(= (list:retain-top-k-keyed $entries $count)
   (retain-top-k-keyed-atom $entries $count))

(= (list:key-by $key-function $xs)
   (map-atom $xs $item
     (let $key ($key-function $item)
       (let $value (cons-atom $item ())
         (cons-atom $key $value)))))

(= (list:unkey $entries)
   (unkey-atom $entries))

; Schwartzian order-statistic filtering: evaluate the key function exactly
; once per occurrence, select in the native keyed representation, and return
; the selected values in their original occurrence order.
(= (list:retain-top-k-by-number $key-function $xs $count)
   (function
     (chain (eval $xs) $__list_top_k_items
       (chain (eval $count) $__list_top_k_count
         (eval
           (_minimal-retain-top-k-by-number
             $__list_top_k_items $__list_top_k_count $__list_top_k_item
             (eval ($key-function $__list_top_k_item))))))))

; The native path may fuse keying, bounded selection, and key erasure only
; after proving that every item has one pure key.  This equation remains the
; complete relational meaning when that proof is unavailable.
(= (_minimal-retain-top-k-by-number $xs $count $item $key)
   (chain
     (eval
       (map-atom $xs $item
         (let $score $key
           (let $value (cons-atom $item ())
             (cons-atom $score $value)))))
     $keyed
     (chain
       (retain-top-k-keyed-atom $keyed $count)
       $retained
       (chain (unkey-atom $retained) $result
         (return $result)))))

; The default order is stable shortlex over common parseable Atom syntax.
; Domain orders such as numeric order, KBO, or WPO remain explicit through
; sort-by rather than being confused with the default serialization order.
(= (list:sort $xs)
   (sort-atom $xs))

; Stable merge sort over expression-backed finite sequences.  The ordering
; argument is a binary predicate denoting "left precedes or ties right".
; Keeping ties on the left makes equal-key occurrences retain their order.
(= (list:split-half $xs)
   (list:split-half-walk $xs $xs ()))

(= (list:split-half-walk $slow $fast $left-reversed)
   (if (== $fast ())
       (let $left (eval (list:reverse $left-reversed))
         ($left $slow))
       (let ($fast-head $fast-tail) (decons-atom $fast)
         (if (== $fast-tail ())
             (let $left (eval (list:reverse $left-reversed))
               ($left $slow))
             (let ($slow-head $slow-tail) (decons-atom $slow)
               (let ($fast-second $fast-rest) (decons-atom $fast-tail)
                 (let $next-left
                      (eval (cons-atom $slow-head $left-reversed))
                   (eval
                     (list:split-half-walk
                       $slow-tail $fast-rest $next-left)))))))))

(= (list:merge-by $precedes $left $right)
   (if (== $left ())
       $right
       (if (== $right ())
           $left
           (let ($left-head $left-tail) (decons-atom $left)
             (let ($right-head $right-tail) (decons-atom $right)
               (let $take-left
                    (eval ($precedes $left-head $right-head))
                 (if $take-left
                     (let $rest
                          (eval
                            (list:merge-by
                              $precedes $left-tail $right))
                       (eval (cons-atom $left-head $rest)))
                     (let $rest
                          (eval
                            (list:merge-by
                              $precedes $left $right-tail))
                       (eval (cons-atom $right-head $rest))))))))))

(= (list:sort-by $precedes $xs)
   (if (== $xs ())
       ()
       (let ($head $tail) (decons-atom $xs)
         (if (== $tail ())
             $xs
             (let ($left $right) (eval (list:split-half $xs))
               (let $sorted-left (eval (list:sort-by $precedes $left))
                 (let $sorted-right (eval (list:sort-by $precedes $right))
                   (eval
                     (list:merge-by
                       $precedes $sorted-left $sorted-right)))))))))

(= (list:number-ascending $left $right)
   (<= $left $right))

(= (list:number-descending $left $right)
   (>= $left $right))

(= (list:sort-numbers $xs)
   (sort-numbers-atom $xs))

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
