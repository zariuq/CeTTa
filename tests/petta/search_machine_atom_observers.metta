!(car-atom (a b c))
!(cdr-atom (a b c))
!(cdr-atom (only))

; Shared evaluator atom constructors are ordinary strict PeTTa functions.
; Their argument computations must finish before the host operation observes
; the resulting expression shape.
(= (observer-head) a)
(= (observer-tail) (b c))
(= (observer-list) (a b c))
(= (observer-cons-computed)
   (cons-atom (observer-head) (observer-tail)))
(= (observer-decons-computed)
   (decons-atom (observer-list)))

!(observer-cons-computed)
!(observer-decons-computed)

; The source occurrence plan decides whether an expression is a computation
; or list data before size-atom observes its value.  The second call is a
; negative conformance canary: upstream PeTTa produces no answer for this
; quoted relational path, so CeTTa must not invent one.
!(size-atom (and implies iff))
(= (observer-size $items)
   (size-atom $items))
!(observer-size (quote (and implies iff)))

; The retained branch constructs a list around a recursive computation; the
; discarded branch is the negative witness that the tail is really evaluated.
(= (observer-retain-a $items)
   (if (== (size-atom $items) 0)
       ()
       (let ($head $tail) (decons-atom $items)
         (if (== $head a)
             (cons-atom $head (observer-retain-a $tail))
             (observer-retain-a $tail)))))

!(observer-retain-a (a b a c))
