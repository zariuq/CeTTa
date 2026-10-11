; Derived rules return lists of primitive proof commands. They gain no
; privilege in the checker. HOL:Aux keys keep their intermediate proofs local.

(= (hol-light:sym-commands $key $type $left $premise)
  (HOL:Cons (HOL:Step (HOL:Aux $key 0)
      (REFL (Const "=" (hol-light:eq-type $type))))
  (HOL:Cons (HOL:Step (HOL:Aux $key 1)
      (MK_COMB (HOL:Aux $key 0) $premise))
  (HOL:Cons (HOL:Step (HOL:Aux $key 2) (REFL $left))
  (HOL:Cons (HOL:Step (HOL:Aux $key 3)
      (MK_COMB (HOL:Aux $key 1) (HOL:Aux $key 2)))
  (HOL:Cons (HOL:Step $key (EQ_MP (HOL:Aux $key 3) (HOL:Aux $key 2))) HOL:Nil))))))

(= (hol-light:beta-commands $key $variable $body $argument)
  (HOL:Cons (HOL:Step (HOL:Aux $key 0)
      (BETA (Comb (Abs $variable $body) $variable)))
  (HOL:Cons (HOL:Step $key
      (INST (HOL:Cons (HOL:Maplet $argument $variable) HOL:Nil) (HOL:Aux $key 0))) HOL:Nil)))

(= (hol-light:receipt-count $result)
  (case $result
    (((Some (HOL:State $operators $constants $axioms $cache $receipts))
      (hol-light:length $receipts))
     ($refused None))))

(= (hol-light:dependencies $result $key)
  (case $result
    (((Some $state)
      (case (hol-light:known $state $key)
        (((Some (HOL:Thm $hyps $conclusion $dependencies $origin)) $dependencies)
         ($absent None))))
     ($refused None))))

(= (hol-light:premise-count $result $key)
  (case $result
    (((Some (HOL:State $operators $constants $axioms $cache $receipts))
      (hol-light:receipt-premise-count $receipts $key))
     ($refused None))))

(= (hol-light:receipt-premise-count $receipts $key)
  (case $receipts
    ((HOL:Nil None)
     ((HOL:Cons (HOL:Receipt $origin $request $premises) $rest)
      (if (== $origin $key) (hol-light:length $premises)
        (hol-light:receipt-premise-count $rest $key)))
     ($malformed None))))
