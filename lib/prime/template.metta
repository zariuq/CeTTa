; Templates passed as values (Prime).
;
; A template a caller passes to a library is code: a quotation with a slot,
; named by a binder key as a lambda's binder is named.  The library opens it
; by `lift let`, once per use, and runs the instance with `*`.  This is the
; form a library uses in place of writing out map-atom's or foldl-atom's
; definitions so that their parameter is the caller's name: the slot is
; named by the key the caller gives, never by a spelling the library shares.
;
; (template:map L K C): for each element of L, the code C with the
; element's code in its slot K, run where the call stands; the expression of
; the values, in order.
(= (template:map $xs $key $code)
   (map-atom $xs $element *(lift let $key (lift $element) $code)))

; (template:foldl L I A X C): the left fold of the code C over L from I; the
; slots A and X take the accumulator's and the element's codes.
(= (template:foldl $xs $init $acc $item $code)
   (foldl-atom $xs $init $so-far $element
     *(lift let ($acc $item) ((lift $so-far) (lift $element)) $code)))
