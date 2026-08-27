; A pure Prime relation uses the shared owned-frontier controller without
; changing its occurrence bag or bypassing Prime's clause-use accounting.
; Space reads keep this on the relational machine rather than the closed
; prepared-pure evaluator.
!(bind! &prime-controller:first (new-space queue))
!(bind! &prime-controller:second (new-space queue))
!(add-atom &prime-controller:first (answer first))
!(add-atom &prime-controller:second (answer second))

(= (prime-controller:choice)
   (match &prime-controller:first (answer $value) $value))
(= (prime-controller:choice)
   (match &prime-controller:second (answer $value) $value))

!(prime-controller:choice)
