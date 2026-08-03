; Open cons constraints are relational list views.  Failed unification is the
; false result of PeTTa's =/3 predicate, while successful unification retains
; the tail binding.
(= (eq-cons-false) (= 42 (cons Error $_)))
(= (eq-cons-open) (= (Error 5 BadType) (cons Error $tail)))
(= (eq-cons-exact)
   (= (Error 5 BadType) (cons Error (5 BadType))))

!(test (eq-cons-false) false)
!(test (eq-cons-open) true)
!(test (eq-cons-exact) true)
!(test (case (1 2 3) (((cons $head $tail) $head))) 1)

; A retained relational cons view is a logical list, not a three-field
; implementation tuple, when it crosses into append or a ground list fold.
(= (append-cons-view (cons $head $tail))
   (append (x y) (cons $head $tail)))
(= (sum-step $acc $item) (+ $acc $item))
(= (fold-cons-view (cons $head $tail))
   (foldl-atom (cons $head $tail) 0 sum-step))

!(test (append-cons-view (a b)) (x y a b))
!(test (fold-cons-view (1 2)) 3)

(= (if-error $value $yes $no)
   (if (= $value (cons Error $_)) $yes $no))

; Known arithmetic is strict in PeTTa.  Valid operands pass through catch;
; invalid operands become Error values that the relational pattern detects.
!(test (let $value (catch (+ 40 2))
             (if-error $value Error $value))
       42)
!(test (let $value (catch (+ 40 not-a-number))
             (if-error $value Error $value))
       Error)
