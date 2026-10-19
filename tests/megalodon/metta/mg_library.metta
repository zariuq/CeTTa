; Source libraries: a Proofgold HF document, exported once by upstream
; megalodon -sexprinfo (tests/megalodon/fixtures/pfghf_logic.sexpr), is
; checked natively; an imported theorem is reused by a later native proof;
; the theory is saved as a CeTTa library and reloaded into a fresh theory,
; rechecked, without the export or any external process.
!(import! &self megalodon)
!(let $state (mg:new-theory-state hf) (bind! &hf $state))
(= (T) (mg:use (get-state &hf)))
(= (AUTH) (mg:authority (get-state &hf)))
; The authority accepts the document's axioms explicitly.
!(mg:import (AUTH) "tests/megalodon/fixtures/pfghf_logic.sexpr" (axioms all))
; Imported objects are named by content identity; lookup finds them by
; their source name, with provenance.
!(mg:lookup (T) "andE")
(= (andE) "7b2e02f7b16b4025d23cc225cb002a66efbb3e6e8cdd4e9414f2f95aa5f6f1f4")
(= (hf-and $p $q) (Tm.app (Tm.app (Tm.named "87fba1d2da67f06ec37e7ab47c3ef935ef8137209b42e40205afb5afd835b738") $p) $q))
(= (truth) (Tm.named "5867641425602c707eaecd5be95229f6fd709c9b58d50af108dfe27cb49ac069"))

; Native reuse: forall A B. and A B -> A, from the imported andE.
(= (and-left)
   (Tm.all (Tp.prop) (Tm.all (Tp.prop) (Tm.imp (hf-and (Tm.db 1) (Tm.db 0)) (Tm.db 1)))))
(= (and-left-proof)
   (Pf.termLam (Tp.prop) (Pf.termLam (Tp.prop)
     (Pf.proofLam (hf-and (Tm.db 1) (Tm.db 0))
       (Pf.proofApp
         (Pf.termApp (Pf.proofApp (Pf.termApp (Pf.termApp (Pf.known (andE)) (Tm.db 1)) (Tm.db 0)) (Pf.hyp 0))
                     (Tm.db 1))
         (Pf.proofLam (Tm.db 1) (Pf.proofLam (Tm.db 0) (Pf.hyp 1))))))))
!(mg:statement (mg:prove (T) "and_left" (and-left) (and-left-proof)))
!(mg:dependencies (T) "and_left")
; An LCF theorem published as a known keeps its derivation as rows: lazy
; evidence, which the library stores marked (conversion lazy).
!(mg:publish (T) (mg:term-app (mg:term-app (mg:known (T) "and_left") (truth)) (truth)) "and_left_truth")

; Save, then reload into a fresh HF theory: everything is rechecked.
!(mg:save-library (T) "runtime/megalodon-test/pfghf_logic.mglib")
!(let $state (mg:new-theory-state hf) (bind! &again $state))
!(mg:load-library (mg:authority (get-state &again)) "runtime/megalodon-test/pfghf_logic.mglib" (axioms all))
!(mg:statement (mg:known (mg:use (get-state &again)) "and_left"))
!(mg:statement (mg:known (mg:use (get-state &again)) "and_left_truth"))
; The reloaded library is used by a later proof: and A B -> A at A := B := True.
!(mg:statement
   (mg:term-app (mg:term-app (mg:known (mg:use (get-state &again)) "and_left") (truth)) (truth)))
; A library is not reloaded into a theory of another profile, and the
; authority's policy still decides which axioms enter: a load stops at the
; first item it does not accept, keeping the items before it.
!(let $state (mg:new-theory-state egal) (bind! &egal $state))
!(case (mg:try-load-library (mg:authority (get-state &egal)) "runtime/megalodon-test/pfghf_logic.mglib" (axioms all))
   (((Error $call $why) (refused $why)) ($report (accepted $report))))
!(mg:inventory (mg:use (get-state &egal)))
!(let $state (mg:new-theory-state hf) (bind! &strict $state))
!(case (mg:try-load-library (mg:authority (get-state &strict)) "runtime/megalodon-test/pfghf_logic.mglib" (axioms))
   (((Error $call $why) (refused $why)) ($report (accepted $report))))
!(mg:revision (mg:use (get-state &strict)))

; Withdrawing an imported declaration withdraws everything that consulted
; it, the later native proof included; a theorem value from before stays a
; theorem of its old snapshot only.
!(let $old (mg:known (T) "and_left")
   (let $gone (mg:withdraw (AUTH) "87fba1d2da67f06ec37e7ab47c3ef935ef8137209b42e40205afb5afd835b738")
     ($gone (mg:is-theorem $old) (mg:is-valid $old)
      (case (mg:try-known (T) "and_left") (((Error $call $why) (refused $why)) ($t (accepted $t)))))))
!(mg:lookup (T) "andE")
!(mg:lookup (T) "TrueI")
