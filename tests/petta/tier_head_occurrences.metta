; A head's call of an operation the relations do not define (a builtin, or a
; typed relation) runs against the argument it meets, after the structural
; match: its value unifies with that argument, as a relation's would.

(: wrap (-> Atom Atom))
(= (wrap $x) (w $x))
(= (unw (wrap $x)) $x)
(= (split (cons-atom $h $t)) ($h $t))
(= (rebuild (decons-atom $e)) $e)
(= (both (cons-atom $h $t) (wrap $x)) ($h $t $x))
(= (first-of (cons-atom $h $t)) $h)
(= (first-of ()) none)

!(collapse (unw (w 5)))
!(collapse (unw (v 5)))
!(collapse (split (a b c)))
!(collapse (split ()))
!(collapse (rebuild (a (b c))))
!(collapse (both (1 2) (w q)))
!(collapse (first-of (x y)))
!(collapse (first-of ()))

; A higher-order relation specialized to its function argument: the
; specialized equations run where the general ones would.
(= (best-of $rank $best $items)
   (if (== $items ())
       $best
       (let* (($head (car-atom $items))
              ($tail (cdr-atom $items)))
             (if (> ($rank $head) ($rank $best))
                 (best-of $rank $head $tail)
                 (best-of $rank $best $tail)))))
(= (rank (item $x)) $x)
(= (rank none) -1)

!(best-of rank none ((item 3) (item 7) (item 5)))
!(best-of rank none ())
!(collapse (best-of rank (item 9) ((item 3) (item 7))))
