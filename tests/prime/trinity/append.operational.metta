(set:profile hol)

; Lists of numbers and their append, run.  The two datatypes and append are
; declared in &self, so plain evaluation runs append by its two equations,
;   append nil ys         = ys
;   append (cons a as) ys = cons a (append as ys)
; wherever a call occurs, until only nil and cons are left: that list is the
; value.  A value runs to itself, and running gives one answer.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self list (u 0) (: nil list) (: cons (-> num list list)))
!(set:define &self append (-> list list list)
   (= (append nil $ys) $ys)
   (= (append (cons $a $as) $ys) (cons $a (append $as $ys))))
; Why running ends: append was admitted as recursion on its first argument,
; a list; the nil equation has no recursive call and the cons equation
; recurses on field 1 of cons, the tail.  The record is quoted so that it is
; read as data and not run.
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self append))
   (quote $termination))
; The record also names the Lean theorem the admission rests on: structural
; recursion has a set model.  Lean states it for a function of one argument;
; append takes two, which the record says.
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self append))
   (quote $lean))

; [0] ++ [1] runs to [0, 1].
!(append (cons zero nil) (cons (suc zero) nil))
; Both ways of nesting three appends run to [0, 1, 0].
!(append (append (cons zero nil) (cons (suc zero) nil)) (cons zero nil))
!(append (cons zero nil) (append (cons (suc zero) nil) (cons zero nil)))
; One value, two costs: the left nesting takes five firings of append's
; equations, the right nesting four.  Each count is printed with the term it
; counts.
!(let (quote ($values $n)) (lift (cost:firings (append (append (cons zero nil) (cons (suc zero) nil)) (cons zero nil))))
   (quote (firings-of (append (append (cons zero nil) (cons (suc zero) nil)) (cons zero nil)) $values $n)))
!(let (quote ($values $n)) (lift (cost:firings (append (cons zero nil) (append (cons (suc zero) nil) (cons zero nil)))))
   (quote (firings-of (append (cons zero nil) (append (cons (suc zero) nil) (cons zero nil))) $values $n)))
; An append inside a list runs, and so do appends on both sides of an append.
!(cons (suc (suc zero)) (append (cons zero nil) (cons (suc zero) nil)))
!(append (append nil (cons zero nil)) (append nil (cons (suc zero) nil)))
; append l nil runs to l.
!(append nil nil)
!(append (cons zero (cons (suc zero) nil)) nil)

; A value runs to itself.
!(cons zero (cons (suc zero) nil))
; An expression and its value are the same answer,
!(== (append (cons zero nil) (cons (suc zero) nil)) (cons zero (cons (suc zero) nil)))
!(== (append (append (cons zero nil) (cons (suc zero) nil)) (cons zero nil))
     (append (cons zero nil) (append (cons (suc zero) nil) (cons zero nil))))
; and not another list.
!(== (append (cons zero nil) (cons (suc zero) nil)) (cons (suc zero) (cons zero nil)))
; Running gives exactly one answer.
!(collapse (append (append nil (cons zero nil)) (append nil (cons (suc zero) nil))))
; Equality of sets in &self accepts the value that running reached.
!(set:eq &self (append (cons zero nil) (cons (suc zero) nil)) (cons zero (cons (suc zero) nil)))

; A number is not a list: the call is refused with a type error, not run.
!(append zero nil)

; A declaration made in another space does not run here.
!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &s list (u 0) (: nil list) (: cons (-> num list list)))
!(set:define &s concat (-> list list list)
   (= (concat nil $ys) $ys)
   (= (concat (cons $a $as) $ys) (cons $a (concat $as $ys))))
; Plain evaluation in &self does not know concat: the call stays as written,
!(concat (cons zero nil) (cons (suc zero) nil))
; while equality in the space of the declaration computes it.
!(set:eq &s (concat (cons zero nil) (cons (suc zero) nil)) (cons zero (cons (suc zero) nil)))
!(set:eq &s (concat (cons zero nil) (cons (suc zero) nil)) (cons zero nil))
