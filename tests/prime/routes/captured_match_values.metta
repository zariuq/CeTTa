; Captured computed values retain their structural meaning in a let
; pattern. Matching neither repeats the source nor mixes sibling outcomes.
(= (emit $goal) (Rule P $goal))
(= (consume $goal)
   (let (Rule $proof $goal) (emit $goal) (Found $proof $goal)){$goal})
!(consume (Goal 7))
!(let $goal (Goal 7) (consume $goal))
(= (goal) (Goal 7))
!(consume (goal))
!(let $goal (goal) (consume $goal))
(= (counted-goal)
   (let $_ (println! SOURCE_ONCE) (Goal 7)))
!(let $goal (counted-goal) (consume $goal))
!(let $goal (superpose ((Goal A) (Goal B))) (consume $goal))
