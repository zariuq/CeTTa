; ============================================================================
; ROMAN QUERY AUDIT --- audit, top ties, and lightweight pruning
; ============================================================================
;
; This file keeps policy explicit:
;   - QueryAudit      returns all matching candidates,
;   - QueryTopTies    returns all top-confidence ties,
;   - QueryPrunedAudit drops only weaker same-strength candidates.
;
; No default selection policy is imposed here.
;
; Important runtime note:
;   These exact counts are CeTTa-specific. PeTTa reaches a different audit
;   frontier under the same numeric maxsteps on this benchmark.
; ============================================================================

!(import! &self ../lib/lib_pln_query_audit)

(= (STV A) (stv 0.5 0.9))
(= (STV B) (stv 0.25 0.9))
(= (STV C) (stv 0.25 0.9))
(= (STV D) (stv 0.5 0.9))

(= (kb)
   ((Sentence ((Inheritance A B) (stv 0.25 0.9)) (1))
    (Sentence ((Inheritance B D) (stv 0.5  0.9)) (2))
    (Sentence ((Inheritance A C) (stv 0.25 0.9)) (3))
    (Sentence ((Inheritance C D) (stv 0.5  0.9)) (4))))

(= (audit15)
   (PLN.QueryAudit (kb) (Inheritance A D) 15))

(= (top15)
   (PLN.QueryTopTiesFromAudit (audit15)))

(= (audit25)
   (PLN.QueryAudit (kb) (Inheritance A D) 25))

(= (pruned25)
   (PLN.QueryPrunedAuditFromAudit (audit25)))

!(println! "===========================================================")
!(println! " ROMAN QUERY AUDIT --- budget 15 and 25")
!(println! "===========================================================")
!(println! ("audit15 :" (audit15)))
!(println! ("top15   :" (top15)))
!(println! ("audit25 :" (audit25)))
!(println! ("pruned25:" (pruned25)))

!(assertEqual (TupleCount (audit15)) 3)
!(assertEqual (PLN.TupleContains (audit15) ((stv 0.5 0.9) (1 2))) True)
!(assertEqual (PLN.TupleContains (audit15) ((stv 0.5 0.9) (3 4))) True)
!(assertEqual (TupleCount (top15)) 2)
!(assertEqual (PLN.TupleContains (top15) ((stv 0.5 0.9) (1 2))) True)
!(assertEqual (PLN.TupleContains (top15) ((stv 0.5 0.9) (3 4))) True)
!(assertEqual (TupleCount (audit25)) 5)
!(assertEqual (TupleCount (pruned25)) 3)
!(assertEqual (PLN.TupleContains (pruned25) ((stv 0.5 0.9473684210526316) (1 2 3 4))) True)
!(assertEqual (PLN.TupleContains (pruned25) ((stv 0.125 0.486) (1 2))) True)
!(assertEqual (PLN.TupleContains (pruned25) ((stv 0.5 0.9) (1 2))) False)
!(assertEqual (PLN.TupleContains (pruned25) ((stv 0.5 0.9) (3 4))) False)

!(println! "All Roman query audit checks passed.")
