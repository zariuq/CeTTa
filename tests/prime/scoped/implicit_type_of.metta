(set:profile hol)

; A type query on a term whose implicit parameters are not all determined
; answers that it is undetermined, as `try` shows it, rather than standing
; as written: (cons nil nil) is a list of lists of some element type, and
; nothing in the term fixes which.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:inductive &self (List (: A set)) set (: nil (List A)) (: cons (-> A (List A) (List A))))
!(type:of (cons nil nil))
!(try (type:of (cons nil nil)))
; Determined, the answer is the list type at that parameter.
!(type:of (cons zero nil))
!(type:of (cons (cons zero nil) nil))
; Written explicitly, the parameters are given.
!(type:of (cons (List num) (nil num) (nil (List num))))
; Against a type, the parameter is fixed and the judgment decides.
!(type:check (cons nil nil) (List (List num)))
!(type:check (cons nil nil) (List num))
; A term with no type at all still stands as written.
!(type:of (undeclared-thing))
