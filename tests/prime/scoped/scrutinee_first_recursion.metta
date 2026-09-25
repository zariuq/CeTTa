; A recursive definition whose calls change an argument before the scrutinee,
; such as an accumulator, is admitted through its scrutinee-first form: that
; form takes the scrutinee first and recurses structurally while every other
; argument may change, and the authored definition passes its arguments to it.
; The authored equations then hold by conversion.
!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: elem (u 0)) (: a elem) (: b elem) (: c elem)))
!(set:inductive &thy list (u 0) (: nil list) (: cons (-> elem list list)))
!(set:inductive &thy num (u 0) (: zero num) (: succ (-> num num)))

; Reversal onto an accumulator written before the list.
!(set:define &thy rev-onto (-> list list list)
   (= (rev-onto $acc nil) $acc)
   (= (rev-onto $acc (cons $x $xs)) (rev-onto (cons $x $acc) $xs)))
!(set:known-proposition &thy rev-onto)
!(set:known-proposition &thy rev-onto~scrutinee-first)
; The record keeps the termination evidence each definition was admitted on:
; the scrutinee position, its inductive type, and for each constructor the
; fields its recursive calls descend on.
!(let (definition-evidence $rules $termination) (set:known-proof &thy rev-onto) $termination)
!(let (definition-evidence $rules $termination) (set:known-proof &thy rev-onto~scrutinee-first) $termination)
!(set:eq &thy (rev-onto nil (cons a (cons b (cons c nil)))) (cons c (cons b (cons a nil))))
!(set:eq &thy (rev-onto nil (cons a (cons b nil))) (cons a (cons b nil)))
!(set:eq &thy (rev-onto (cons c nil) (cons a nil)) (cons a (cons c nil)))

; Addition that moves successors onto an accumulator written first.
!(set:define &thy add-onto (-> num num num)
   (= (add-onto $acc zero) $acc)
   (= (add-onto $acc (succ $n)) (add-onto (succ $acc) $n)))
!(set:eq &thy (add-onto (succ zero) (succ (succ zero))) (succ (succ (succ zero))))
!(set:eq &thy (add-onto zero (succ zero)) zero)
!(let (definition-evidence $rules $termination) (set:known-proof &thy add-onto) $termination)

; A fixed prefix is admitted directly, with no scrutinee-first form.
!(set:define &thy snoc-each (-> elem list list)
   (= (snoc-each $e nil) (cons $e nil))
   (= (snoc-each $e (cons $x $xs)) (cons $x (snoc-each $e $xs))))
!(set:known-proposition &thy snoc-each~scrutinee-first)
!(let (definition-evidence $rules $termination) (set:known-proof &thy snoc-each) $termination)
!(set:eq &thy (snoc-each c (cons a nil)) (cons a (cons c nil)))

; The arguments before the scrutinee may form a dependent telescope: the
; scrutinee's written binder moves to the front, the others keep their names.
!(set:define &thy apply-n (-> (: A (u 0)) (: f (-> A A)) (: acc A) (: n num) A)
   (= (apply-n $A $f $acc zero) $acc)
   (= (apply-n $A $f $acc (succ $n)) (apply-n $A $f ($f $acc) $n)))
!(set:known-proposition &thy apply-n~scrutinee-first)
!(set:eq &thy (apply-n num succ zero (succ (succ zero))) (succ (succ zero)))
!(set:eq &thy (apply-n num succ (succ zero) (succ zero)) (succ (succ zero)))
!(set:eq &thy (apply-n num succ zero (succ zero)) zero)
!(let (definition-evidence $rules $termination) (set:known-proof &thy apply-n) $termination)

; A scrutinee binder named like an earlier binder would capture the later
; references once moved: the definition is left undetermined.
!(let $r (try (set:define &thy shadow-n (-> (: x num) (: x num) num)
   (= (shadow-n $a zero) $a)
   (= (shadow-n $a (succ $n)) (shadow-n (succ $a) $n)))) $r)

; A definition with no scrutinee is admitted as one equation.
!(set:define &thy plus-two (-> num num) (= (plus-two $n) (succ (succ $n))))
!(let (definition-evidence $rules $termination) (set:known-proof &thy plus-two) $termination)

; The scrutinee-first form is still checked: a call on the same argument is
; not structural, whatever else it changes.
!(set:define &thy spin-onto (-> list list list)
   (= (spin-onto $acc nil) $acc)
   (= (spin-onto $acc (cons $x $xs)) (spin-onto (cons $x $acc) (cons $x $xs))))

; The authored equations are checked in their own contexts before the
; scrutinee-first form is built: a call whose changed argument is ill-typed
; is refused as the authored equation, in the authored context of
; accumulator, head and tail, not as the transformed one.
!(let $r (try (set:define &thy bad-step (-> list list list)
   (= (bad-step $acc nil) $acc)
   (= (bad-step $acc (cons $x $xs)) (bad-step (succ $acc) $xs)))) $r)
