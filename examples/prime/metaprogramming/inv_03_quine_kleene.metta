; Self-reference with code values: a quine and Kleene's recursion combinator.
;
; diag(@c) = @(c[x := @c]) substitutes a code value's own quotation for its
; slot x.  The program (diag @(diag x)) evaluates to its own code, and running
; that code gives the same code again.  (kleene F) steps to a program R that
; runs as (F @R): F receives the code of the very program it is part of
; (Kleene's second recursion theorem).  Substitution here is the equation-level
; `lift let` prototype; Prime has no native `lift` yet.  The prototype is not
; capture-avoiding; here the slot x never occurs under a binder, so it is
; exact.

(: replace (-> (Data Atom) (Data Atom) (Data Atom) (Data Atom)))
(= (replace $s $v $t)
   (if (noreduce-eq $t $s)
       $v
       (if (== (get-metatype $t) Expression)
           (replace-children $s $v $t)
           $t)))
(: replace-children (-> (Data Atom) (Data Atom) (Data Atom) (Data Atom)))
(= (replace-children $s $v $t)
   (if (noreduce-eq $t ())
       ()
       (let ($h $rest) (decons-atom $t)
         (if (noreduce-eq $h quote)
             $t
             (let $h2 (replace $s $v $h)
               (let $rest2 (replace-children $s $v $rest)
                 (cons-atom $h2 $rest2)))))))
(= (lift let $s (quote $a) (quote $c))
   (let $r (replace $s $a $c) (quote $r)))

(= (diag $c) (lift let x @$c $c))
(= (kleene $f) ($f (diag @($f (diag x)))))

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
