; A native proof: an explicitly selected authority, a proof tree checked and
; published in one transaction, LCF steps over retained theorems, reuse of
; the published theorem, portable evidence, and refusals that are not
; theorems.  The same file runs in HE and PeTTa.
!(import! &self megalodon)
!(let $state (mg:new-theory-state custom) (bind! &th $state))
(= (T) (mg:use (get-state &th)))
(= (AUTH) (mg:authority (get-state &th)))
(= (A) (Tm.named "A"))
(= (B) (Tm.named "B"))
(= (ctx-A) (Megalodon.context 0 (types) (hypotheses (Tm.named "A"))))

; The authority selects exactly two propositional parameters.
!(mg:parameter (AUTH) "A" (Tp.prop))
!(mg:parameter (AUTH) "B" (Tp.prop))
!(mg:inventory (T))

; A -> A from a proof tree, checked and published as the known A_imp_A.
!(mg:statement (mg:prove (T) "A_imp_A" (Tm.imp (A) (A)) (Pf.proofLam (A) (Pf.hyp 0))))
!(mg:dependencies (T) "A_imp_A")

; The same proposition by two LCF steps: hyp, then proofLam.
!(mg:statement (mg:proof-lam (mg:hyp (T) (ctx-A) 0)))

; Reuse: the published known, applied by modus ponens under hypothesis A.
!(mg:statement (mg:proof-app (mg:known-in (T) (ctx-A) "A_imp_A") (mg:hyp (T) (ctx-A) 0)))

; A proof tree in an explicit context.
!(mg:statement (mg:check (T) (ctx-A) (Pf.proofApp (Pf.known "A_imp_A") (Pf.hyp 0))))

; Portable evidence: ordinary rows, rechecked natively into a new theorem.
!(mg:evidence (mg:known (T) "A_imp_A"))
!(mg:statement (mg:check-evidence (T) (mg:evidence (mg:proof-lam (mg:hyp (T) (ctx-A) 0)))))
; Evidence records its profile, theory label and revision; it is never
; rechecked in a theory of another profile.
!(let $state (mg:new-theory-state hf) (bind! &hf $state))
!(case (mg:try-check-evidence (mg:use (get-state &hf)) (mg:evidence (mg:known (T) "A_imp_A")))
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))

; Refusals are errors, never theorems.  The try- forms make the error the
; value, so a program can branch on it.
!(case (mg:try-prove (T) "bad" (Tm.imp (A) (B)) (Pf.proofLam (A) (Pf.hyp 0)))
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))
!(case (mg:try-hyp (T) (mg:empty-context) 0)
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))
!(case (mg:try-proof-app (mg:hyp (T) (ctx-A) 0) (mg:hyp (T) (ctx-A) 0))
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))
; A failed proof published nothing.
!(mg:inventory (T))
!(case (mg:try-known (T) "bad")
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))

; Only the authority extends the selected authority.
!(case (mg:try-axiom (T) "C" (A))
   (((Error $call $why) (refused $why)) ($done (accepted $done))))
!(mg:axiom (AUTH) "ax_B" (B))
!(mg:statement (mg:known (T) "ax_B"))
