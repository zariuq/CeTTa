; Self-reference with code values: a quine and Kleene's recursion combinator.
;
; diag(@c) = @(c[x := @c]) substitutes a code value's own quotation for its
; slot x.  The program (diag @(diag x)) evaluates to its own code, and running
; that code gives the same code again.  (kleene F) steps to a program R that
; runs as (F @R): F receives the code of the very program it is part of
; (Kleene's second recursion theorem).  Substitution is the native `lift
; let`, and (lift M) is the code of M's value: a quotation is sealed, so the
; code of a parameter's value enters code by lift, never by quoting the
; parameter.

(= (diag $c) (lift let x (lift $c) $c))
(= (kleene $f) ($f (diag (lift app (lift $f) @(diag x)))))

; --- a quine -------------------------------------------------------------------
; 1. The program evaluates to its own code.
!(diag @(diag x))
; 2. Exactly its own code, compared as written.
!(let $q (diag @(diag x)) (noreduce-eq $q @(diag @(diag x))))
; 3. Running the result reproduces it: a fixed point of activation.
!(let $q (diag @(diag x)) (let $again *$q (noreduce-eq $again $q)))
; 4. Its printed source, through the reflective printer.
!(let $q (diag @(diag x)) (repr $q))

; --- Kleene's recursion combinator ----------------------------------------------
(= (shown $c) (receipt $c))
; 5. F = shown receives the code of the program it runs in.
!(kleene shown)
; 6. That code is exactly the program (shown (diag @(shown (diag x)))).
!(let (receipt $code) (kleene shown)
   (noreduce-eq $code @(shown (diag @(shown (diag x))))))
; 7. Running the received code hands the same code over again.
!(let (receipt $code) (kleene shown)
   (let (receipt $again) *$code (noreduce-eq $again $code)))

; 8. A program that measures itself: it receives its own code, opens the
;    quotation without running it, and reports its head and size.
(= (measure $code)
   (unify $code (quote $body)
     (Self (car-atom $body) (size-atom $body))
     not-code))
!(kleene measure)
