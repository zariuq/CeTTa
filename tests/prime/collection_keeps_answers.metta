; Collection is an implementation step, never an observation: every answer
; stays.  The consistency check below runs the same example list twice
; through a relational program, which grows the evaluation region past its
; first collection budget while an active Need path mixes lifetimes, part of
; it older than the region being reclaimed.

!(import! &self ../../lib/ilp/metagol_core.metta)

!(bind! &gc-kb (new-space))
!(add-atom &gc-kb (MIL.FactPrimitive r-mother mother))
!(add-atom &gc-kb (MIL.FactPrimitive r-father father))
!(add-atom &gc-kb (MIL.Fact mother i a))
!(add-atom &gc-kb (MIL.Fact mother c f))
!(add-atom &gc-kb (MIL.Fact mother c g))
!(add-atom &gc-kb (MIL.Fact mother f h))
!(add-atom &gc-kb (MIL.Fact father a b))
!(add-atom &gc-kb (MIL.Fact father a c))
!(add-atom &gc-kb (MIL.Fact father b d))
!(add-atom &gc-kb (MIL.Fact father b e))

(= (gc:negatives)
   ((MIL.Ex a b) (MIL.Ex b c) (MIL.Ex c d) (MIL.Ex d e)
    (MIL.Ex e f) (MIL.Ex f g) (MIL.Ex g h) (MIL.Ex h i)))
(= (gc:examples)
   (MIL.Examples
     ((MIL.Ex i b) (MIL.Ex i c) (MIL.Ex a d) (MIL.Ex a e)
      (MIL.Ex a f) (MIL.Ex a g) (MIL.Ex c h))
     (gc:negatives)
     MIL.Relational))

(= (gc:grandparent)
   (MIL.Chain (MIL.Choice r-mother r-father) (MIL.Choice r-mother r-father)))

; The same negative examples, checked twice from one bound list.
(= (gc:avoids-twice $sem $program $negatives)
   (and (mil:all-negative $sem $program $negatives)
        (mil:all-negative $sem $program $negatives)))
(= (gc:twice)
   (gc:avoids-twice &gc-kb
     (MIL.Chain (MIL.Choice r-mother r-father) (MIL.Choice r-mother r-father))
     ((MIL.Ex a b) (MIL.Ex b c) (MIL.Ex c d) (MIL.Ex d e)
      (MIL.Ex e f) (MIL.Ex f g) (MIL.Ex g h) (MIL.Ex h i))))
!(gc:twice)

; Full consistency, called from an equation body.
(= (gc:consistent $sem $program) (mil:consistent $sem $program (gc:examples)))
(= (gc:check) (gc:consistent &gc-kb (gc:grandparent)))
!(gc:check)

; A learner step: keep the program only when it is consistent.
(= (gc:accept $sem $program)
   (if (mil:consistent $sem $program (gc:examples)) $program (superpose ())))
(= (gc:learn)
   (let $program (select (gc:accept &gc-kb (gc:grandparent)))
     (Learned $program)))
!(gc:learn)

; A wrong program is still rejected.
(= (gc:reject) (gc:consistent &gc-kb (MIL.Chain r-mother r-mother)))
!(gc:reject)
