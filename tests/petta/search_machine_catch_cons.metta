; Open cons constraints are relational list views.  Failed unification is the
; false result of PeTTa's =/3 predicate, while successful unification retains
; the tail binding.
;
; A declaration may authorize the pure-grounded slot optimizer, but that
; optimizer must not expose the private three-field carrier to size-atom.
(: size-atom (-> $value Number))
(= (declared-size-cons-tail (cons $head $tail))
   (size-atom $tail))

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

; A tail received through the equation pattern remains a value.  By contrast,
; the explicit size-atom declaration above makes the direct callable-headed
; source expression a demanded computation in upstream PeTTa; its non-list
; result is observed as the empty value.
!(test (declared-size-cons-tail (a b c)) 2)
!(test (size-atom (and implies iff)) ())

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

; Error bypasses a typed result check and is caught before it can collapse
; into ordinary search failure.  The second test forces backtracking after a
; normal first answer, so the catch delimiter must be restored with the saved
; continuation.  The third pins both halves of delimitation: an error cuts
; choices created inside catch but preserves an enclosing function choice.
(: typed-choice (-> Number))
(= (typed-choice) 7)
(= (typed-choice) (+ 1 not-a-number))

(: typed-error-first (-> Number))
(= (typed-error-first) (+ 1 not-a-number))
(= (typed-error-first) 8)

(= (mark-caught $value)
   (if-error $value caught $value))
(= (outer-choice)
   (mark-caught (catch (typed-error-first))))
(= (outer-choice) 9)

!(test (mark-caught (catch (typed-error-first))) caught)
!(test (collapse (mark-caught (catch (typed-choice)))) (7 caught))
!(test (collapse (outer-choice)) (caught 9))
