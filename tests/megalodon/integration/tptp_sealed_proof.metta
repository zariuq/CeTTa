; A small interpretation of TPTP propositional data, checked independently
; in the Egal/HOTG and Mizar profiles. Parsing never admits an axiom.
; This example deliberately handles just p, q and implication.
!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self megalodon)
!(import! &self seal)

(= (demo:term p) (Tm.named "p"))
(= (demo:term q) (Tm.named "q"))
(= (demo:term (→ $a $b)) (Tm.imp (demo:term $a) (demo:term $b)))
(= (demo:claim [(fof $_name conjecture $formula)]) (demo:term $formula))
(= (demo:claim [(fof $_name axiom $_formula)]) (Demo.UnsupportedRole axiom))
(= (demo:identity) (Pf.proofLam (Tm.named "p") (Pf.hyp 0)))

!(let $s (mg:new-theory-state egal) (bind! &egal $s))
!(let $s (mg:new-theory-state mizar) (bind! &mizar $s))
(= (E) (mg:use (get-state &egal)))
(= (M) (mg:use (get-state &mizar)))
!(mg:parameter (mg:authority (get-state &egal)) "p" (Tp.prop))
!(mg:parameter (mg:authority (get-state &egal)) "q" (Tp.prop))
!(mg:parameter (mg:authority (get-state &mizar)) "p" (Tp.prop))
!(mg:parameter (mg:authority (get-state &mizar)) "q" (Tp.prop))

!(mg:statement (mg:prove (E) "parsed-identity"
   (demo:claim (tptp:read-text "fof(identity,conjecture,(p => p))."))
   (demo:identity)))
!(mg:statement (mg:prove (M) "parsed-identity"
   (demo:claim (tptp:read-text "fof(identity,conjecture,(p => p))."))
   (demo:identity)))
; A genuine proof of p => p cannot certify the different parsed claim p => q.
!(case (mg:try-prove (E) "wrong"
   (demo:claim (tptp:read-text "fof(wrong,conjecture,(p => q))."))
   (demo:identity))
   (((Error $call $why) (wrong-claim-refused $why)) ($v unexpected-acceptance)))
; An axiom record has no conjecture interpretation and gains no authority.
!(case (mg:try-prove (E) "unselected-axiom"
   (demo:claim (tptp:read-text "fof(unselected,axiom,p)."))
   (demo:identity))
   (((Error $call $why) (axiom-not-admitted $why)) ($v unexpected-acceptance)))
; Portable evidence is checked again; a profile label is preserved.
!(mg:statement (mg:check-evidence (E) (mg:evidence (mg:known (E) "parsed-identity"))))
!(case (mg:try-check-evidence (M) (mg:evidence (mg:known (E) "parsed-identity")))
   (((Error $call $why) (cross-profile-refused $why)) ($v unexpected-acceptance)))

; Store a theorem as an opaque MeTTa value, and expose a separate portable
; view. A snapshot reader must never receive a nested theorem capability.
!(bind! &proofs (new-space))
!(let $t (mg:known (E) "parsed-identity") (add-atom &proofs (certificate $t)))
!(mg:statement (match &proofs (certificate $t) $t))
!(bind! &views (new-space))
!(let $j (mg:statement (mg:known (E) "parsed-identity")) (add-atom &views (statement $j)))
!(let $v (seal:new-service proof-view) (bind! &service (new-state $v)))
(= (OPEN) (let (seal:service $o $a $s) (get-state &service) $o))
(= (ADMIN) (let (seal:service $o $a $s) (get-state &service) $a))
(= (LOCK) (let (seal:service $o $a $s) (get-state &service) $s))
!(let $v (seal:new-key) (bind! &key (new-state $v)))
(= (KEY) (get-state &key))
!(seal:grant (ADMIN) (KEY) always)
!(let $box (seal:lock (LOCK) (KEY) &views)
   (seal:read-space (seal:reader (OPEN)) $box (KEY)))
!(let $box (seal:lock (LOCK) (KEY) &proofs)
   (case (seal:try-read-space (seal:reader (OPEN)) $box (KEY))
     (((Error $call $why) (nested-theorem-refused $why)) ($v leaked-theorem))))
