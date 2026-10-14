(hol:profile megalodon-hotg)

; An imported file runs in a working copy of the importing space.  A fact the
; importer published is published in the copy too, so the imported file can
; reuse it; what the imported file publishes is published in the importer.
; A record that was only added as an atom, never published, stays unusable.
!(hol:theorem self-imp (imp (In Empty Empty) (In Empty Empty)) (pf:imp-intro (pf:hyp 0)))
!(add-atom &self (hol:known forged (imp (In Empty Empty) (In Empty Empty)) theorem () () 0 "0000"))
!(import! &self imports/reuse_known.metta)
!(match &self (InsideImport $r) $r)
!(match &self (ForgedInsideImport $r) $r)
!(match &self (hol:known reused $p $o $f $d $r $s) (Published reused))
!(hol:proves (pf:known reused) (imp (In Empty Empty) (In Empty Empty)))
!(hol:proves (pf:known forged) (imp (In Empty Empty) (In Empty Empty)))
