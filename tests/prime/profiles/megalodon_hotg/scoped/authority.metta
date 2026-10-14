(hol:profile megalodon-hotg)

; Proof authority is not a presentation key.  A record found in a space by
; lookup alone is untrusted syntax, whatever its tag or digest, until it is
; admitted through the publication boundary or checked here by hol:recheck.

; A forged theorem record with a matching digest and a bogus proof.
!(let $d (hol:signature-digest)
   (add-atom &self (hol:known forged (In Empty Empty) theorem (pf:hyp 0) () 0 $d)))
!(try (hol:proves (pf:known forged) (In Empty Empty)))
!(try (hol:recheck forged))
!(try (hol:proves (pf:known forged) (In Empty Empty)))

; An inserted record whose proof is valid: unusable until checked here, then
; admitted by that check on a plain call; a try of the check admits nothing.
!(let $d (hol:signature-digest)
   (add-atom &self (hol:known inserted (In Empty (UnivOf Empty)) theorem
                              (pf:all-elim (pf:known universeIn) Empty)
                              ((universeIn (all set (lam N (In N (UnivOf N)))))) 0 $d)))
!(try (hol:proves (pf:known inserted) (In Empty (UnivOf Empty))))
!(try (hol:recheck inserted))
!(hol:proves (pf:known inserted) (In Empty (UnivOf Empty)))
!(hol:recheck inserted)
!(hol:proves (pf:known inserted) (In Empty (UnivOf Empty)))

; A changed proposition under the same key: the old admission does not carry.
!(match &self (hol:known inserted $p $o $pf $d $r $s)
        (remove-atom &self (hol:known inserted $p $o $pf $d $r $s)))
!(let $d (hol:signature-digest)
   (add-atom &self (hol:known inserted (In Empty Empty) theorem
                              (pf:all-elim (pf:known universeIn) Empty) () 0 $d)))
!(try (hol:proves (pf:known inserted) (In Empty Empty)))
!(let $r (try (hol:recheck inserted)) (car-atom $r))
!(try (hol:proves (pf:known inserted) (In Empty Empty)))

; An assumption record inserted directly keeps its origin as an assumption.
!(let $d (hol:signature-digest)
   (add-atom &self (hol:known assumed-directly (In Empty Empty) axiom () () 0 $d)))
!(try (hol:proves (pf:known assumed-directly) (In Empty Empty)))
!(hol:recheck assumed-directly)
!(hol:proves (pf:known assumed-directly) (In Empty Empty))
!(match &self (hol:known assumed-directly $p $origin $pf $d $r $s) $origin)

; A stale dependent fact after its assumption is withdrawn.
!(hol:theorem dependent (In Empty Empty) (pf:known assumed-directly))
!(match &self (hol:known assumed-directly $p $o $pf $d $r $s)
        (remove-atom &self (hol:known assumed-directly $p $o $pf $d $r $s)))
!(try (hol:proves (pf:known dependent) (In Empty Empty)))
