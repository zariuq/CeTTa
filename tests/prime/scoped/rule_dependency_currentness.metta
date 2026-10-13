(hol:profile core)

; A theorem keeps the logical capabilities and named propositions its proof
; actually uses. Withdrawing one invalidates direct and transitive reuse,
; including saved native packages, while unrelated proofs remain available.
!(import! &self pf)
!(hol:theorem independent (all prop (lam p (imp p p))) (pf:fix p (pf:assume h h)))
!(add-atom &self (hol:rule eq-subst))
!(hol:theorem transport (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))) (pf:fix p (pf:fix q (pf:assume e (pf:assume h (pf:eq-subst (lam r r) e h))))))
!(hol:theorem transport-cited (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))) (pf:known transport))
!(match &self (hol:known transport $p $o $f $d $r $s) (DirectDependencies $d))
!(match &self (hol:known transport-cited $p $o $f $d $r $s) (NestedDependencies (map-atom $d $entry (car-atom $entry))))
!(pf:rests-on &self transport-cited)
!(let (HolNativeProofV1 $n $p $f $t $ty $c $r $rows $d) (hol:native-proof transport) (NativeAssumptions $rows))
!(let $package (hol:native-proof transport-cited) (add-atom &self (saved-native $package)))
!(let $package (hol:native-proof transport) (let $linked (hol:native-link identity $package) (add-atom &self (saved-linked $linked))))
!(car-atom (try (hol:proves (pf:known transport-cited) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))))))
!(car-atom (try (hol:recheck transport)))
!(car-atom (try (hol:native-proof transport-cited)))

; Imported records must preserve every dependency the rechecked proof uses.
; A rule dependency omitted directly, or hidden behind an omitted citation,
; cannot be laundered through recheck. Extra current pins are harmless.
!(match &self (hol:known transport $p $o $f $d $r $s) (add-atom &self (hol:known stripped-rule $p $o $f () $r $s)))
!(try (hol:recheck stripped-rule))
!(hol:recheck stripped-rule)
!(match &self (hol:known transport-cited $p $o $f $d $r $s) (add-atom &self (hol:known stripped-citation $p $o $f () $r $s)))
!(try (hol:recheck stripped-citation))
!(hol:recheck stripped-citation)
!(match &self (hol:known transport $p $o $f $d $r $s) (add-atom &self (hol:known wrong-rule-pin $p $o $f (((hol:rule eq-subst) (hol:rule other))) $r $s)))
!(try (hol:recheck wrong-rule-pin))
!(match &self (hol:known transport $p $o $f $d $r $s) (let $extra (cons-atom (independent (all prop (lam p (imp p p)))) $d) (add-atom &self (hol:known extra-pins $p $o $f $extra $r $s))))
!(match &self (hol:known extra-pins $p $o $f $d $r $s) (ExtraDependencies $d))
!(hol:recheck extra-pins)
!(car-atom (try (hol:proves (pf:known extra-pins) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))))))

; The original signature is unchanged. Only the used rule is withdrawn.
!(remove-atom &self (hol:rule eq-subst))
!(try (hol:proves (pf:known transport) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (hol:proves (pf:known transport-cited) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (hol:proves (pf:known extra-pins) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (hol:proves (pf:known stripped-rule) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (hol:proves (pf:known stripped-citation) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (hol:proves (pf:fix p (pf:fix q (pf:assume e (pf:assume h (pf:eq-subst (lam r r) e h))))) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (hol:recheck transport-cited))
!(try (hol:native-proof transport-cited))
!(match &self (saved-native $package)
   (let (HolNativeProofV1 $n $p $f $t $ty $c $r $rows $d) $package
     (try (hol:native-use $package (Lam $ty (idx 0))))))
!(match &self (saved-native $package)
   (let (HolNativeProofV1 $n $p $f $t $ty $c $r $rows $d) $package
     (try (hol:native-normalize $package (Lam $ty (idx 0))))))
!(match &self (saved-native $package) (try (hol:native-link identity $package)))
!(match &self (saved-linked $package)
   (let (HolNativeLinkedV1 $i $p $t $ty $c $r $rows) $package
     (try (hol:native-use $package (Lam $ty (idx 0))))))
!(car-atom (try (hol:proves (pf:known independent) (all prop (lam p (imp p p))))))
!(car-atom (try (hol:native-proof independent)))

; A diagnostic recheck changes no admission. Restoring the exact rule
; restores those proofs. A failed plain recheck withdraws the receipt until
; a subsequent successful plain recheck.
!(add-atom &self (hol:rule eq-subst))
!(car-atom (try (hol:proves (pf:known transport-cited) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))))))
!(car-atom (try (hol:native-proof transport-cited)))
!(remove-atom &self (hol:rule eq-subst))
!(hol:recheck transport)
!(add-atom &self (hol:rule eq-subst))
!(try (hol:proves (pf:known transport) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (hol:proves (pf:known transport-cited) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(hol:recheck transport)
!(car-atom (try (hol:proves (pf:known transport-cited) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))))))

; Named assumptions have the same completeness requirement. In particular,
; a proof of Falsum from an explicit assumption must not survive removal
; of that assumption by importing a stripped record.
!(hol:axiom premise Falsum)
!(hol:theorem from-premise Falsum (pf:known premise))
!(match &self (hol:known from-premise $p $o $f $d $r $s) (add-atom &self (hol:known stripped-premise $p $o $f () $r $s)))
!(try (hol:recheck stripped-premise))
!(hol:recheck stripped-premise)
!(match &self (hol:known premise $p $o $f $d $r $s) (remove-atom &self (hol:known premise $p $o $f $d $r $s)))
!(try (hol:proves (pf:known from-premise) Falsum))
!(try (hol:proves (pf:known stripped-premise) Falsum))
