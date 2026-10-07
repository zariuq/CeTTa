(set:profile hol)

; Why it matters: generated code (from a model, a search or a user) can be
; admitted only after the kernel checks it against a declared interface, so
; unreadable, ill-typed or out-of-interface code never runs.
;
; Candidate programs arrive as text.  Each is read with `parse` and checked
; with `type:check` against the interface (-> nat nat).  Only an accepted
; candidate is run, on the specification's examples.  The first candidate that
; meets them is installed as an ordinary function, and its text is printed
; back with `repr`.

!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:define &self plus (-> nat nat nat)
   (= (plus zero $n) $n)
   (= (plus (succ $m) $n) (succ (plus $m $n))))

; Unary numerals and ordinary numbers.
(= (to-num zero) 0)
(= (to-num (succ $n)) (+ 1 (to-num $n)))
(= (from-num $k) (if (== $k 0) zero (succ (from-num (- $k 1)))))

; The specification: f(0) = 1, f(1) = 3, f(2) = 5.
(= (meets-spec $f)
   (and (== (to-num ($f (from-num 0))) 1)
        (and (== (to-num ($f (from-num 1))) 3)
             (== (to-num ($f (from-num 2))) 5))))

; Candidates, as text from an untrusted source.
(= (candidate 1) "(lam x (plus x x))")
(= (candidate 2) "(lam x (succ zero zero))")
(= (candidate 3) "(lam x (println! x))")
(= (candidate 4) "(lam x (plus x")
(= (candidate 5) "(lam x (plus x (succ x)))")
(= (candidate 6) "(lam x (succ (plus x x)))")

; A read failure is an Error value; collecting the reader's answers keeps it
; as data to inspect instead of letting it end the run.
(= (intake $i)
   (let $text (candidate $i)
     (let $read (collapse (parse $text))
       (unify $read ((Error $call $why))
         (Rejected $i unreadable)
         (let $term (car-atom $read)
           (let $verdict (type:check $term (-> nat nat))
             (if (== $verdict True)
                 (if (meets-spec $term) (Accepted $i $term) (Rejected $i fails-examples))
                 (if (== $verdict False)
                     (Rejected $i ill-typed)
                     (Rejected $i outside-interface)))))))))

!(intake 1)
!(intake 2)
!(intake 3)
!(intake 4)
!(intake 5)
!(intake 6)

; Install the first accepted candidate as an ordinary function.
(= (first-accepted $i)
   (if (> $i 6) none
       (let $r (intake $i)
         (unify $r (Accepted $n $term) $term (first-accepted (+ $i 1))))))
!(let $f (first-accepted 1)
   (let $added (add-atom &self (= (twice-plus-one $n) ($f $n)))
     (Installed (repr $f))))
!(to-num (twice-plus-one (from-num 10)))
