; Proof authority is not a presentation key.  A record found in a space by
; lookup alone is untrusted syntax, whatever its tag or digest, until it is
; admitted through the publication boundary or checked here by set:recheck.

; A forged theorem record with a matching digest and a bogus proof.
!(let $d (set:signature-digest)
   (add-atom &self (set:known forged (In Empty Empty) theorem (pf:hyp 0) () 0 $d)))
!(try (set:proves (pf:known forged) (In Empty Empty)))
!(try (set:recheck forged))
!(try (set:proves (pf:known forged) (In Empty Empty)))

; An inserted record whose proof is valid: unusable until checked here, then
; admitted by that check on a plain call; a try of the check admits nothing.
!(let $d (set:signature-digest)
   (add-atom &self (set:known inserted (In Empty (Power Empty)) theorem
                              (pf:all-elim (pf:known PowerI) Empty)
                              ((PowerI (all set (lam x (In x (Power x)))))) 0 $d)))
!(try (set:proves (pf:known inserted) (In Empty (Power Empty))))
!(try (set:recheck inserted))
!(set:proves (pf:known inserted) (In Empty (Power Empty)))
!(set:recheck inserted)
!(set:proves (pf:known inserted) (In Empty (Power Empty)))

; A changed proposition under the same key: the old admission does not carry.
!(match &self (set:known inserted $p $o $pf $d $r $s)
        (remove-atom &self (set:known inserted $p $o $pf $d $r $s)))
!(let $d (set:signature-digest)
   (add-atom &self (set:known inserted (In Empty Empty) theorem
                              (pf:all-elim (pf:known PowerI) Empty) () 0 $d)))
!(try (set:proves (pf:known inserted) (In Empty Empty)))
!(let $r (try (set:recheck inserted)) (car-atom $r))
!(try (set:proves (pf:known inserted) (In Empty Empty)))

; An assumption record inserted directly keeps its origin as an assumption.
!(let $d (set:signature-digest)
   (add-atom &self (set:known assumed-directly (In Empty Empty) axiom () () 0 $d)))
!(try (set:proves (pf:known assumed-directly) (In Empty Empty)))
!(set:recheck assumed-directly)
!(set:proves (pf:known assumed-directly) (In Empty Empty))
!(match &self (set:known assumed-directly $p $origin $pf $d $r $s) $origin)

; A stale dependent fact after its assumption is withdrawn.
!(set:theorem dependent (In Empty Empty) (pf:known assumed-directly))
!(match &self (set:known assumed-directly $p $o $pf $d $r $s)
        (remove-atom &self (set:known assumed-directly $p $o $pf $d $r $s)))
!(try (set:proves (pf:known dependent) (In Empty Empty)))
