; Why it matters: a program synthesizer turns a few labelled examples and a
; library of ordinary functions into a new, readable function that is itself
; ordinary code, with a checkable proof of why it fits the examples.
;
; The library is three ordinary predicates on the numbers 0 to 9.  Their
; evidence is produced once, by running them over the domain.  The learner
; then types lambda hypotheses, smallest first, and proves each against the
; examples from that evidence alone: hypotheses are code values, so no
; library function runs while they are generated or checked, as the call
; counter shows.  The learned lambda is then activated with `*` and run on
; all ten numbers, including ones that were never examples.

!(import! &self ../nupln_lambda/nupln_lambda_core.metta)

; ---- the library, with a call counter
!(bind! &calls (new-space))
(= (called $name $x) (add-atom &calls (call $name $x)))
(= (calls) (let $all (collapse (match &calls (call $f $x) $f)) (size-atom $all)))

(= (even $x) (let $c (called even $x) (== (% $x 2) 0)))
(= (small $x) (let $c (called small $x) (< $x 5)))
(= (square $x) (let $c (called square $x) (== $x (* (isqrt $x) (isqrt $x)))))
(= (isqrt $x) (isqrt-from $x 0))
(= (isqrt-from $x $r) (if (> (* (+ $r 1) (+ $r 1)) $x) $r (isqrt-from $x (+ $r 1))))

; ---- evidence: each predicate run once at each number
!(bind! &kb (new-space))
!(add-atom &kb (nu:domain Num))
!(add-atom &kb (nu:bias ∧))
!(add-atom &kb (nu:bias ¬))
!(add-atom &kb (: even (-> Num Bool)))
!(add-atom &kb (: small (-> Num Bool)))
!(add-atom &kb (: square (-> Num Bool)))

(= (numbers) (0 1 2 3 4 5 6 7 8 9))
(= (truth True) ⊤)
(= (truth False) ⊥)
; The statement is code built from the values: a quotation is sealed, so
; they enter it by lift.
(= (record $p $x $value)
   (let* (($tv (truth $value))
          ($statement (lift app (lift $p) (lift $x))))
     (add-atom &kb (: (ran $p $x) (≞ $statement $tv)))))

!(let $ns (numbers)
   (let $added
        (collapse
          (let $x (superpose $ns)
            (let $u (add-atom &kb (: $x Num))
              (let* (($e (even $x)) ($s (small $x)) ($q (square $x)))
                (let* (($a (record even $x $e))
                       ($b (record small $x $s))
                       ($c (record square $x $q)))
                  done)))))
     (Evidence (size-atom $added) (CallsSoFar (calls)))))

; ---- learning: a number that is even and not small, from seven examples
(= (examples)
   ((Example (6) ⊤) (Example (8) ⊤)
    (Example (2) ⊥) (Example (4) ⊥) (Example (5) ⊥) (Example (7) ⊥) (Example (9) ⊥)))

; The learned program, its sizes, the proof over the seven examples, and the
; call count, which learning leaves unchanged.
!(let (Learned $h $size $cost $proof) (nu:learn &kb (-> Num Bool) (examples) 12 100)
   (let $named (nu:named &kb (x y z) $h)
     (let $stored (add-atom &self (learned $named))
       (Learned (nu:source &kb $h) (TypingSize $size) (ProofSize $cost) $proof
         (CallsAfterLearning (calls))))))

; ---- the learned program as ordinary code on every number
!(let $ns (numbers)
   (match &self (learned $h)
     (let $f *$h
       (let $chosen (collapse (let $x (superpose $ns) (if ($f $x) $x (empty))))
         (Accepted $chosen (CallsAfterUse (calls)))))))
