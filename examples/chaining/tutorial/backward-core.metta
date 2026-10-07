; Proof-producing Horn search. The gate receives a remaining depth and a goal.
; Rule variables are freshly scoped by ordinary match on each activation.
(= (depth-gate $remaining $goal) (> $remaining 0))

(= (bc $gate $remaining $goal)
   (if ($gate $remaining $goal)
       (superpose
         ((match &self (fact $id $goal)
            (if ($gate $remaining $goal) (proof $id $goal) (empty)))
          (match &self (rule $name $premises $goal)
            (let $children (bc-all $gate (- $remaining 1) $premises)
              (if ($gate $remaining $goal)
                  (proof ($name $children) $goal)
                  (empty))))))
       (empty)))

(= (bc-all $gate $remaining $goals)
   (if (== $goals ())
       ()
       (let* (($first (bc $gate $remaining (car-atom $goals)))
              ($rest (bc-all $gate $remaining (cdr-atom $goals))))
         (cons-atom $first $rest))))
