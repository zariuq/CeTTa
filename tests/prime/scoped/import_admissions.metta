; An imported file runs in a working copy of the importing space.  A fact the
; importer published is published in the copy too, so the imported file can
; reuse it; what the imported file publishes is published in the importer.
; A record that was only added as an atom, never published, stays unusable.
!(set:theorem self-imp (imp (In Empty Empty) (In Empty Empty)) (pf:imp-intro (pf:hyp 0)))
!(add-atom &self (set:known forged (imp (In Empty Empty) (In Empty Empty)) theorem () () 0 "0000"))
!(import! &self ./imports/reuse_known.metta)
!(match &self (InsideImport $r) $r)
!(match &self (ForgedInsideImport $r) $r)
!(match &self (set:known reused $p $o $f $d $r $s) (Published reused))
!(set:proves (pf:known reused) (imp (In Empty Empty) (In Empty Empty)))
!(set:proves (pf:known forged) (imp (In Empty Empty) (In Empty Empty)))
