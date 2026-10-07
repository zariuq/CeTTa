(set:profile hol)

; A theorem is cited only under the profile it was checked under.  Its record
; names the signature it was checked against, and a citation, a recheck, a
; native package and the use or link of a package are refused under another
; profile, however the statement reads, with the profile named. A fresh
; space uses prime-common. Unsupported or conflicting selections leave a
; judgment that would cite uninterpreted.
!(import! &self pf)
!(bind! &logic (let $s (new-space) (let $_ (add-atom $s (set:profile hol)) $s)))
!(bind! &hotg (let $s (new-space) (let $_ (add-atom $s (set:profile megalodon-hotg)) $s)))

; The same logical statement checked under each profile, and in each a
; theorem that cites it by name.
!(pf:theorem &logic self-imp (all prop (lam a (imp a a))) (pf:fix a (pf:assume h h)))
!(pf:theorem &hotg self-imp-hotg (all prop (lam a (imp a a))) (pf:fix a (pf:assume h h)))
!(pf:theorem &logic self-imp-again (all prop (lam a (imp a a))) (pf:known self-imp))
!(pf:theorem &hotg self-imp-hotg-again (all prop (lam a (imp a a))) (pf:known self-imp-hotg))
!(let (SetNativeProofV1 $n $p $f $t $type $c $r $rows $d) (set:native-proof &logic self-imp-again)
   (HolCites (map-atom $rows $row (car-atom $row))))
!(let (SetNativeProofV1 $n $p $f $t $type $c $r $rows $d) (set:native-proof &hotg self-imp-hotg-again)
   (HotgCites (map-atom $rows $row (car-atom $row))))
; and a set theorem, which only megalodon-hotg states
!(pf:theorem &hotg empty-in-universe (In Empty (UnivOf Empty))
   (pf:all-elim (pf:known universeIn) Empty))

; A receipt copied from one profile's space into the other's is not cited
; there, nor rechecked, nor can a theorem be published on it.
!(match &hotg (set:known self-imp-hotg $p $o $f $d $r $s)
   (add-atom &logic (set:known self-imp-hotg $p $o $f $d $r $s)))
!(try (set:proves &logic (pf:known self-imp-hotg) (all prop (lam a (imp a a)))))
!(try (set:recheck &logic self-imp-hotg))
!(try (set:theorem &logic on-hotg (quote (all prop (lam a (imp a a))))
                                (quote (pf:known self-imp-hotg))))
!(match &hotg (set:known empty-in-universe $p $o $f $d $r $s)
   (add-atom &logic (set:known empty-in-universe $p $o $f $d $r $s)))
; The hol profile cannot state it; the citation is refused before that.
!(try (set:proves &logic (pf:have e (pf:known empty-in-universe) (pf:imp-intro (pf:hyp 0)))
                         (imp Falsum Falsum)))
!(match &logic (set:known self-imp $p $o $f $d $r $s)
   (add-atom &hotg (set:known self-imp $p $o $f $d $r $s)))
!(try (set:proves &hotg (pf:known self-imp) (all prop (lam a (imp a a)))))

; A package of one profile is neither used nor computed nor linked under the
; other.
!(let $package (set:native-proof &hotg self-imp-hotg)
   (unify $package (SetNativeProofV1 $n $p $f $t $type $c $r $rows $d)
     (try (set:native-use &logic $package (Lam $type (idx 0))))
     NotAPackage))
!(let $package (set:native-proof &logic self-imp)
   (unify $package (SetNativeProofV1 $n $p $f $t $type $c $r $rows $d)
     (try (set:native-normalize &hotg $package (Lam $type (idx 0))))
     NotAPackage))
!(let $package (set:native-proof &logic self-imp)
   (try (set:native-link &hotg identity $package)))

; The same space under another profile: its theorems are not cited, and the
; theorem that cites one has no package.
!(remove-atom &logic (set:profile hol))
!(add-atom &logic (set:profile megalodon-hotg))
!(try (set:proves &logic (pf:known self-imp) (all prop (lam a (imp a a)))))
!(try (set:native-proof &logic self-imp-again))
!(try (set:theorem &logic on-hol (quote (all prop (lam a (imp a a))))
                                (quote (pf:known self-imp))))
; Removing the declaration selects prime-common, which refuses the HOL
; receipt. Conflicting declarations still leave the citation uninterpreted.
!(remove-atom &logic (set:profile megalodon-hotg))
!(set:proves &logic (pf:known self-imp) (all prop (lam a (imp a a))))
!(try (set:native-proof &logic self-imp-again))
!(add-atom &logic (set:profile hol))
!(add-atom &logic (set:profile megalodon-hotg))
!(set:proves &logic (pf:known self-imp) (all prop (lam a (imp a a))))
!(try (set:native-proof &logic self-imp-again))
; The original profile restored, the theorem is cited again.
!(remove-atom &logic (set:profile megalodon-hotg))
!(set:proves &logic (pf:known self-imp) (all prop (lam a (imp a a))))
!(let (SetNativeProofV1 $n $p $f $t $type $c $r $rows $d) (set:native-proof &logic self-imp-again)
   (CitedAgain (map-atom $rows $row (car-atom $row))))
; Of the theorems published here on a citation, only self-imp-again stands:
; a refused citation published nothing.
!(collapse (match &logic (set:known $name $p $o (pf:known $cited) $d $r $s) ($name $cited)))

; A library that selects megalodon-hotg, imported into this hol space, runs
; where the two declarations conflict: it publishes nothing here, and while
; both stand, not even this space's own theorem is cited.
!(set:theorem own-self-imp (all prop (lam a (imp a a))) (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(import! &self imports/hotg_lemma.metta)
!(collapse (match &self (set:profile $p) $p))
!(collapse (match &self (set:known $n $p $o $f $d $r $s) $n))
!(set:proves (pf:known lib-self-imp) (all prop (lam a (imp a a))))
!(set:proves (pf:known own-self-imp) (all prop (lam a (imp a a))))
!(remove-atom &self (set:profile megalodon-hotg))
!(set:proves (pf:known own-self-imp) (all prop (lam a (imp a a))))
