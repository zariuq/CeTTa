(set:profile hol)

; Lists over any set and their append, run.  The lists are declared once,
; with a parameter: the set their elements come from,
;   List : set -> set
;   nil  : (A : set) -> List A
;   cons : (A : set) -> A -> List A -> List A
; and append is defined once for every A by its two equations,
;   append A nil ys         = ys
;   append A (cons a as) ys = cons a (append A as ys).
; The same append then runs on lists of numbers and on lists of lists of
; numbers.  A use may write the parameters, `(cons num zero (nil num))`, or
; leave them out, as MeTTa programs write lists, `(cons zero nil)`.  A use
; without them is elaborated to the use with them before it runs, so both
; run alike, and a value is written with its parameters.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self (List (: A set)) set
   (: nil (List A))
   (: cons (-> A (List A) (List A))))
!(set:define &self append (-> (A : set) (l m : (List A)) (List A))
   (= (append $A (nil $A) $m) $m)
   (= (append $A (cons $A $a $l) $m) (cons $A $a (append $A $l $m))))
; Why running ends: append was admitted as recursion on argument 1, a list
; at the parameter A; the nil equation has no recursive call and the cons
; equation recurses on field 1 of cons, the tail.
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self append))
   (quote $termination))
; The record names the Lean theorem of structural recursion, and says what
; it covers: a function of one argument, on a datatype without parameters.
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self append))
   (quote $lean))
; In the rules that run append, the parameter of the constructor in the
; pattern is matched by a variable of its own, (PVar 2) and (PVar 4): a call
; that is typed has the call's own parameter there.
!(match &self (type:rule append $n $p $r) (lift $p))

; ---- The parameters written.
; [0] ++ [1] at the numbers.
!(append num (cons num zero (nil num)) (cons num (suc zero) (nil num)))
; [[0]] ++ [[], [1]] at the lists of numbers.
!(append (List num)
   (cons (List num) (cons num zero (nil num)) (nil (List num)))
   (cons (List num) (nil num)
         (cons (List num) (cons num (suc zero) (nil num)) (nil (List num)))))
; append l nil runs to l at both.
!(append num (cons num zero (cons num (suc zero) (nil num))) (nil num))
!(append (List num) (cons (List num) (nil num) (nil (List num))) (nil (List num)))
; Three firings: two of the cons equation, one of the nil equation.  The
; count is printed with the term it counts.
!(let (quote ($values $n)) (lift (cost:firings (append num (cons num zero (cons num (suc zero) (nil num))) (nil num))))
   (quote (firings-of (append num (cons num zero (cons num (suc zero) (nil num))) (nil num)) $values $n)))

; ---- The parameters left out, as MeTTa programs write lists.
; A list alone: the parameter is the type of its first element.
!(cons zero (cons (suc zero) nil))
; The same calls as above: each list is read at the list type its place
; in append wants, and runs to the same value.
!(append num (cons zero nil) (cons (suc zero) nil))
!(append (List num) (cons (cons zero nil) nil) (cons nil (cons (cons (suc zero) nil) nil)))
!(append num (cons zero (cons (suc zero) nil)) nil)
; The two spellings are one value, and the same number of firings.
!(== (append num (cons zero nil) nil) (cons num zero (nil num)))
!(let (quote ($values $n)) (lift (cost:firings (append num (cons zero (cons (suc zero) nil)) nil)))
   (quote (firings-of (append num (cons zero (cons (suc zero) nil)) nil) $values $n)))
; A definition written as MeTTa programs write it: no parameter in its
; patterns or on its right sides.  It is admitted as the definition with
; them, and runs.
!(set:define &self snoc (-> (A : set) (l : (List A)) (a : A) (List A))
   (= (snoc $A nil $a) (cons $a nil))
   (= (snoc $A (cons $b $l) $a) (cons $b (snoc $A $l $a))))
!(snoc num (cons zero nil) (suc zero))
!(snoc (List num) nil (cons zero nil))

; Equality of sets in &self accepts the value that running reached, in both
; spellings.
!(set:eq &self (append num (cons num zero (nil num)) (cons num (suc zero) (nil num)))
               (cons num zero (cons num (suc zero) (nil num))))
!(set:eq &self (append num (cons zero nil) (cons (suc zero) nil)) (cons zero (cons (suc zero) nil)))

; ---- Refused with a type error, not run.
; A number is not a set, so it is no parameter of append;
!(append zero nil)
; a number is not a list of numbers;
!(append num zero (nil num))
; and a list of numbers is not a list of lists of numbers.
!(append (List num) (cons num zero (nil num)) (nil (List num)))
; A number is no list, written either way.
!(cons zero zero)
