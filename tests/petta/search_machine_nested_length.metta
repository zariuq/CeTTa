(= (add-atom-no-duplicate $Space $Atom)
   (if (== () (collapse (once (match $Space $Atom $Atom))))
       (add-atom $Space $Atom)
       (empty)))

(= (expand-once)
   (case (match &self (num $term) $term)
         (($value
           (add-atom-no-duplicate
             &self
             (num (S $value)))))))

(= (expand $rounds)
   (if (== $rounds 0)
       done
       (let $ignored (expand-once)
            (expand (- $rounds 1)))))

(= (peano-prefix $rounds)
   (let* (($seed (add-atom &self (num Z)))
          ($expanded (expand $rounds)))
         (match &self (num $value) $value)))

; A surrounding aggregate must not change evaluation ownership or replay the
; effectful search.  The five values are produced once each.
!(collapse (length (collapse (peano-prefix 4))))
!(collapse (match &self (num $value) $value))

; No fifth successor is introduced by observation or backtracking.
!(collapse
    (match &self
           (num (S (S (S (S (S Z))))))
           present))
