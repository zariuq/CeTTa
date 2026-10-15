(hol:profile base)

; A recursive definition whose calls change an argument before the scrutinee,
; such as an accumulator, is admitted through its scrutinee-first form: that
; form takes the scrutinee first and recurses structurally while every other
; argument may change, and the authored definition passes its arguments to it.
; The authored equations then hold by conversion.
!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile base)) $profile-space)))
!(pf:theory &thy ((: elem (u 0)) (: a elem) (: b elem) (: c elem)))
!(hol:inductive &thy list (u 0) (: nil list) (: cons (-> elem list list)))
!(hol:inductive &thy num (u 0) (: zero num) (: succ (-> num num)))

; Reversal onto an accumulator written before the list.
!(hol:define &thy rev-onto (-> list list list)
   (= (rev-onto $acc nil) $acc)
   (= (rev-onto $acc (cons $x $xs)) (rev-onto (cons $x $acc) $xs)))
!(hol:known-proposition &thy rev-onto)
!(hol:known-proposition &thy rev-onto~scrutinee-first)
; The record keeps the termination evidence each definition was admitted on:
; the scrutinee position, its inductive type, and for each constructor the
; fields its recursive calls descend on.
!(let (definition-evidence $rules $termination $lean) (hol:known-proof &thy rev-onto) $termination)
!(let (definition-evidence $rules $termination $lean) (hol:known-proof &thy rev-onto~scrutinee-first) $termination)
!(hol:eq &thy (rev-onto nil (cons a (cons b (cons c nil)))) (cons c (cons b (cons a nil))))
!(hol:eq &thy (rev-onto nil (cons a (cons b nil))) (cons a (cons b nil)))
!(hol:eq &thy (rev-onto (cons c nil) (cons a nil)) (cons a (cons c nil)))

; Addition that moves successors onto an accumulator written first.
!(hol:define &thy add-onto (-> num num num)
   (= (add-onto $acc zero) $acc)
   (= (add-onto $acc (succ $n)) (add-onto (succ $acc) $n)))
!(hol:eq &thy (add-onto (succ zero) (succ (succ zero))) (succ (succ (succ zero))))
!(hol:eq &thy (add-onto zero (succ zero)) zero)
!(let (definition-evidence $rules $termination $lean) (hol:known-proof &thy add-onto) $termination)

; A fixed prefix is admitted directly, with no scrutinee-first form.
!(hol:define &thy snoc-each (-> elem list list)
   (= (snoc-each $e nil) (cons $e nil))
   (= (snoc-each $e (cons $x $xs)) (cons $x (snoc-each $e $xs))))
!(hol:known-proposition &thy snoc-each~scrutinee-first)
!(let (definition-evidence $rules $termination $lean) (hol:known-proof &thy snoc-each) $termination)
!(hol:eq &thy (snoc-each c (cons a nil)) (cons a (cons c nil)))

; The arguments before the scrutinee may form a dependent telescope: the
; scrutinee's written binder moves to the front, the others keep their names.
!(hol:define &thy apply-n (-> (: A (u 0)) (: f (-> A A)) (: acc A) (: n num) A)
   (= (apply-n $A $f $acc zero) $acc)
   (= (apply-n $A $f $acc (succ $n)) (apply-n $A $f ($f $acc) $n)))
!(hol:known-proposition &thy apply-n~scrutinee-first)
!(hol:eq &thy (apply-n num succ zero (succ (succ zero))) (succ (succ zero)))
!(hol:eq &thy (apply-n num succ (succ zero) (succ zero)) (succ (succ zero)))
!(hol:eq &thy (apply-n num succ zero (succ zero)) zero)
!(let (definition-evidence $rules $termination $lean) (hol:known-proof &thy apply-n) $termination)

; A scrutinee binder named like an earlier binder would capture the later
; references once moved: the definition is left undetermined.
!(let $r (try (hol:define &thy shadow-n (-> (: x num) (: x num) num)
   (= (shadow-n $a zero) $a)
   (= (shadow-n $a (succ $n)) (shadow-n (succ $a) $n)))) $r)

; A definition with no scrutinee is admitted as one equation.
!(hol:define &thy plus-two (-> num num) (= (plus-two $n) (succ (succ $n))))
!(let (definition-evidence $rules $termination $lean) (hol:known-proof &thy plus-two) $termination)

; The scrutinee-first form is still checked: a call on the same argument is
; not structural, whatever else it changes.
!(hol:define &thy spin-onto (-> list list list)
   (= (spin-onto $acc nil) $acc)
   (= (spin-onto $acc (cons $x $xs)) (spin-onto (cons $x $acc) (cons $x $xs))))

; The authored equations are checked in their own contexts before the
; scrutinee-first form is built: a call whose changed argument is ill-typed
; is refused as the authored equation, in the authored context of
; accumulator, head and tail, not as the transformed one.
!(let $r (try (hol:define &thy bad-step (-> list list list)
   (= (bad-step $acc nil) $acc)
   (= (bad-step $acc (cons $x $xs)) (bad-step (succ $acc) $xs)))) $r)
