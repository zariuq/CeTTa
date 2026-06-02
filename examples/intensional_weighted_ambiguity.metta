; ============================================================================
; INTENSIONAL WEIGHTED AMBIGUITY --- audit and top ties for trait evidence
; ============================================================================
;
; This experiment focuses on ambiguous evidence for ONE property inside a trait:
;   casey has two equally confident but differently strong observations for
;   shows-up-on-time, so PropertyBeliefTopTies should surface BOTH.
;
; It also checks that the new explicit-space reducer path agrees with the
; default intensional audit path.
; ============================================================================

!(import! &self ../lib/lib_wmpln)
!(import! &self ../lib/lib_overlap_corrected_revision)
!(import! &self ../lib/lib_intensional_weighted)

!(bind! &kb (new-space))
!(bind! &pkt_casey (new-space))
!(bind! &meta_casey (new-space))

!(add-atom &kb (ConceptProp reliable-lite shows-up-on-time 1.0))
!(add-atom &kb (ConceptProp reliable-lite returns-messages 0.5))

!(add-atom &kb (Sentence ((Inheritance casey (IntSet shows-up-on-time)) (stv 0.8 0.9)) (501)))
!(add-atom &kb (Sentence ((Inheritance casey (IntSet shows-up-on-time)) (stv 0.3 0.9)) (502)))
!(add-atom &kb (Sentence ((Inheritance casey (IntSet returns-messages)) (stv 0.7 0.8)) (503)))

(= (shows-up-audit)
   (PropertyBeliefAudit &kb casey shows-up-on-time 30))

(= (shows-up-top)
   (PropertyBeliefTopTies &kb casey shows-up-on-time 30))

(= (casey-reliable-audit)
   (IntensionalMemberAudit &kb casey reliable-lite 30))

(= (casey-reliable-audit-explicit)
   (IntensionalMemberAuditInSpaces &pkt_casey &meta_casey &kb casey reliable-lite 30))

!(assertEqual (TupleCount (shows-up-audit)) 2)
!(assertEqual (TupleCount (shows-up-top)) 2)
!(assertEqual (PLN.TupleContains (shows-up-top) ((stv 0.8 0.9) (501))) True)
!(assertEqual (PLN.TupleContains (shows-up-top) ((stv 0.3 0.9) (502))) True)
!(assertEqual (casey-reliable-audit-explicit) (casey-reliable-audit))

!(println! "==========================================================")
!(println! "INTENSIONAL WEIGHTED AMBIGUITY --- Casey / reliable-lite")
!(println! "==========================================================")
!(println! ("shows-up-audit" (shows-up-audit)))
!(println! ("shows-up-top" (shows-up-top)))
!(println! ("casey-reliable-audit" (casey-reliable-audit)))
!(println! ("casey-reliable-audit-explicit" (casey-reliable-audit-explicit)))
!(println! "CeTTa intensional ambiguity checks passed.")
