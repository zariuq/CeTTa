; Ground calls in C0 use admission-time LHS matching and RHS instantiation
; templates. The reference matcher remains authoritative for every decline.
(= (c0-repeat $same $same) (repeat $same))
(= (c0-nested (pair $left (box $right)) $left)
   (nested $right))

; A nonlinear mismatch must try the next occurrence in declaration order.
(= (c0-choice $same $same) impossible)
(= (c0-choice $left $right) (possible $left $right))

; Identical source occurrences retain exact multiplicity.
(= (c0-duplicate $value) (duplicate $value))
(= (c0-duplicate $value) (duplicate $value))

; An RHS-local variable is outside C0 and must retain reference semantics.
(= (c0-local $value) (local $fresh))

; An open caller-visible alias is outside C0 even when its clause has a
; compiled representation.
(= (c0-open $value) $value)

!(c0-repeat marker marker)
!(c0-nested (pair left (box right)) left)
!(c0-choice left right)
!(collapse (c0-duplicate marker))
!(let (local $value) (c0-local marker)
   (local-body-variable (is-var $value)))
!(let $answer (c0-open $outer)
   (open-caller-alias (== $answer $outer)))
