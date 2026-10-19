; Failed forgeries.  Only the native kernel makes theorems: printed text,
; a capsule of another brand with the same nickname, an equation a program
; adds to a library name, a theorem of another theory and a theorem whose
; dependencies were withdrawn are all refused where authority is needed.
!(import! &self megalodon)
!(import! &self seal)
!(let $state (mg:new-theory-state custom) (bind! &th $state))
!(let $state (mg:new-theory-state custom) (bind! &other $state))
(= (T) (mg:use (get-state &th)))
(= (AUTH) (mg:authority (get-state &th)))
(= (A) (Tm.named "A"))
(= (B) (Tm.named "B"))
(= (closed) (Megalodon.context 0 (types) (hypotheses)))
!(mg:parameter (AUTH) "A" (Tp.prop))
!(mg:parameter (AUTH) "B" (Tp.prop))
!(mg:statement (mg:prove (T) "A_imp_A" (Tm.imp (A) (A)) (Pf.proofLam (A) (Pf.hyp 0))))

; 1. A theorem prints as (Sealed megalodon-theorem); that text is data.
!(mg:known (T) "A_imp_A")
!(mg:is-theorem (mg:known (T) "A_imp_A"))
!(mg:is-theorem (Sealed megalodon-theorem))
!(case (mg:try-publish (T) (Sealed megalodon-theorem) "forged")
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))
!(case (mg:try-proof-app (Sealed megalodon-theorem) (mg:hyp (T) (Megalodon.context 0 (types) (hypotheses (Tm.named "A"))) 0))
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))
!(case (mg:try-publish-claim (T) (Megalodon.judgment (closed) (B)) "forged" (B))
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))

; 2. A brand of the seal library nicknamed megalodon-theorem: its capsule
; prints the same and is not a theorem.
!(let (seal:facets $sealer $unsealer) (seal:new megalodon-theorem)
   (let $box (seal:seal $sealer (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses)) (Tm.named "B")))
     (case (mg:try-publish (T) $box "forged")
       (((Error $call $why) ($box (mg:is-theorem $box) (refused $why)))
        ($theorem (accepted $theorem))))))

; ... and the unsealer of that brand opens no theorem capsule.
!(let (seal:facets $sealer $unsealer) (seal:new megalodon-theorem)
   (collapse (seal:unseal $unsealer (mg:known (T) "A_imp_A"))))

; 3. An equation added for a library name adds answers, not authority: the
; forged answer is refused where a theorem is needed.
(= (mg:assumption $theory $context $goal) (Sealed megalodon-theorem))
!(case (mg:try-publish-claim (T) (mg:run Tac.assumption (T) (closed) (B)) "forged" (B))
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))

; 4. Authority-only operations refuse the theory value.
!(case (mg:try-parameter (T) "C" (Tp.prop))
   (((Error $call $why) (refused $why)) ($done (accepted $done))))

; 5. A theorem of another theory.
!(mg:parameter (mg:authority (get-state &other)) "A" (Tp.prop))
!(case (mg:try-publish (T) (mg:proof-lam (mg:hyp (mg:use (get-state &other)) (Megalodon.context 0 (types) (hypotheses (Tm.named "A"))) 0)) "imported")
   (((Error $call $why) (refused $why)) ($theorem (accepted $theorem))))

; Nothing above was published.
!(mg:inventory (T))

; 6. Withdrawing A withdraws what consulted it; a theorem value from before
; is no longer valid and cannot be published.
!(let $old (mg:proof-lam (mg:hyp (T) (Megalodon.context 0 (types) (hypotheses (Tm.named "A"))) 0))
   (let $gone (mg:withdraw (AUTH) "A")
     (case (mg:try-publish (T) $old "stale")
       (((Error $call $why) ($gone (mg:is-valid $old) (refused $why)))
        ($theorem (accepted $theorem))))))
!(mg:inventory (T))
