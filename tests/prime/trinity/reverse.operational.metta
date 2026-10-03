; Two reversals of a list, run.  Over the lists of numbers and their append,
;   rev nil                  = nil
;   rev (cons a as)          = append (rev as) (cons a nil)
;   rev-onto acc nil         = acc
;   rev-onto acc (cons x xs) = rev-onto (cons x acc) xs
; Both reverse a list: rev l and rev-onto nil l run to one value.  They are
; different programs and they cost differently.  A firing is one call
; rewritten by one equation; on a list of n numbers rev takes
; (n + 1)(n + 2) / 2 firings, and the reversal onto an accumulator n + 1.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self list (u 0) (: nil list) (: cons (-> num list list)))
!(set:define &self append (-> list list list)
   (= (append nil $ys) $ys)
   (= (append (cons $a $as) $ys) (cons $a (append $as $ys))))
!(set:define &self rev (-> list list)
   (= (rev nil) nil)
   (= (rev (cons $a $as)) (append (rev $as) (cons $a nil))))
; rev-onto recurses on its second argument.  It is admitted as written by way
; of a definition with the list first, which carries the recursion; rev-onto
; passes its two arguments to it (reverse.intensional shows the record).  It
; runs as written: passing the arguments is part of the step of the equation
; that then applies, and the result reads as rev-onto again.
!(set:define &self rev-onto (-> list list list)
   (= (rev-onto $acc nil) $acc)
   (= (rev-onto $acc (cons $x $xs)) (rev-onto (cons $x $acc) $xs)))
; The same reversal written with the list first.
!(set:define &self rev-onto-list-first (-> list list list)
   (= (rev-onto-list-first nil $acc) $acc)
   (= (rev-onto-list-first (cons $x $xs) $acc)
      (rev-onto-list-first $xs (cons $x $acc))))
; A wrong reversal: it puts each number back in front.
!(set:define &self wrong-rev (-> list list)
   (= (wrong-rev nil) nil)
   (= (wrong-rev (cons $a $as)) (append (cons $a nil) (wrong-rev $as))))

; ---- One value.
; The empty list and the lists [0], [0, 1] and [0, 1, 2], reversed both ways.
!(rev nil)
!(rev-onto nil nil)
!(rev (cons zero nil))
!(rev-onto nil (cons zero nil))
!(rev (cons zero (cons (suc zero) nil)))
!(rev-onto nil (cons zero (cons (suc zero) nil)))
!(rev (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))))
!(rev-onto nil (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))))
!(rev-onto-list-first (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))) nil)
; The two expressions are the same answer, and not the list itself.
!(== (rev (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))))
     (rev-onto nil (cons zero (cons (suc zero) (cons (suc (suc zero)) nil)))))
!(== (rev (cons zero (cons (suc zero) nil))) (cons zero (cons (suc zero) nil)))
; Reversing twice gives the list back, mixing the two reversals.
!(rev (rev-onto nil (cons zero (cons (suc zero) (cons (suc (suc zero)) nil)))))
; The accumulator is put after the reversed list.
!(rev-onto (cons zero nil) (cons (suc zero) (cons (suc (suc zero)) nil)))

; ---- Different costs.
; (cost:firings e) runs e once and gives its values with the number of
; firings the run took, a firing being one call rewritten by one equation of
; a declared definition.  On [0, 1, 2]: ten firings by rev, four with the
; list first.
!(cost:firings (rev (cons zero (cons (suc zero) (cons (suc (suc zero)) nil)))))
!(cost:firings (rev-onto-list-first (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))) nil))
; The counts at the lengths 0, 1, 2 and 3.
(: count-firings (-> Atom Number))
(= (count-firings $e) (let ($values $n) (cost:firings $e) $n))
; rev: 1, 3, 6, 10, the quadratic (n + 1)(n + 2) / 2.
!((count-firings (rev nil))
  (count-firings (rev (cons zero nil)))
  (count-firings (rev (cons zero (cons (suc zero) nil))))
  (count-firings (rev (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))))))
; With the list first: 1, 2, 3, 4, the linear n + 1.  At the empty list the
; two costs agree; the difference begins at length one.
!((count-firings (rev-onto-list-first nil nil))
  (count-firings (rev-onto-list-first (cons zero nil) nil))
  (count-firings (rev-onto-list-first (cons zero (cons (suc zero) nil)) nil))
  (count-firings (rev-onto-list-first (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))) nil)))
; rev-onto as written: the same counts, one step for each of its equations.
!((count-firings (rev-onto nil nil))
  (count-firings (rev-onto nil (cons zero nil)))
  (count-firings (rev-onto nil (cons zero (cons (suc zero) nil))))
  (count-firings (rev-onto nil (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))))))
; A value takes no step, and a call refused for its argument takes none.
!(cost:firings (cons zero (cons (suc zero) nil)))
!(cost:firings (rev zero))
; At a list whose tail is unknown, rev-onto takes the one step its second
; equation gives, and the result reads as rev-onto; with no equation left that
; applies, the call stays as written.
!(add-atom &self (: some-list list))
!(cost:firings (rev-onto nil (cons zero some-list)))
!(rev-onto (cons zero nil) some-list)

; ---- The wrong reversal is the identity.
; It runs every list to itself,
!(wrong-rev nil)
!(wrong-rev (cons zero nil))
!(wrong-rev (cons zero (cons (suc zero) nil)))
!(wrong-rev (cons zero (cons (suc zero) (cons (suc (suc zero)) nil))))
; so it agrees with rev on the lists of at most one number, and not on [0, 1].
!(== (wrong-rev (cons zero nil)) (rev (cons zero nil)))
!(== (wrong-rev (cons zero (cons (suc zero) nil))) (rev (cons zero (cons (suc zero) nil))))
