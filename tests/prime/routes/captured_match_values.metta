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

; Reading a stored field returns data even through a compiled wrapper.
; Equal symbol heads may occupy different physical Atom objects.
!(bind! &field-space (new-space))
!(add-atom &field-space (Row (+ 1 2)))
!(add-atom &field-space (Nested (Box (+ 1 2))))
(= (read-field) (match &field-space (Row $code) $code))
(= (read-nested-field) (match &field-space (Nested (Box $code)) $code))
!(+ (match &field-space (Row $code) $code) 1)
!(let $value (match &field-space (Row $code) $code) (+ $value 1))
!(+ (read-field) 1)
!(+ (read-nested-field) 1)
; Explicit evaluation and an authored computation template still execute.
!(+ (eval (read-field)) 1)
!(+ (match &field-space (Row (+ $a $b)) (+ $a $b)) 1)
