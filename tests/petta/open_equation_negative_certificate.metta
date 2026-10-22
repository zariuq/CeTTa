; A relation outside the compiled fragment stays rejected while unrelated
; equations are appended.  Its dependency certificate should be refreshed
; without compiling the same rejected source after every append.
(= (negative-certificate-target $space $expr)
   (if (== (collapse
             (callPredicate (Predicate (cyclic_term $expr)))) ())
       (add-atom $space $expr)
       (empty)))

(= (negative-certificate-loop $n)
   (if (== $n 0)
       done
       (let $_added
         (add-atom &self (= (negative-certificate-unrelated $n) $n))
         (let $_value
           (negative-certificate-target &self
             (negative-certificate-row $n))
           (negative-certificate-loop (- $n 1))))))

!(negative-certificate-loop 200)

; A relevant append must invalidate the refusal certificate and expose the
; new equation on the following call.
!(add-atom &self
   (= (negative-certificate-target $space special) changed))
!(collapse (negative-certificate-target &self special))
