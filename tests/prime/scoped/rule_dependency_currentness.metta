(set:profile hol)

; A theorem keeps the logical capabilities and named propositions its proof
; actually uses. Withdrawing one invalidates direct and transitive reuse,
; including saved native packages, while unrelated proofs remain available.
!(import! &self pf)
!(set:theorem independent (all prop (lam p (imp p p))) (pf:fix p (pf:assume h h)))
!(add-atom &self (set:rule eq-subst))
!(set:theorem transport (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))) (pf:fix p (pf:fix q (pf:assume e (pf:assume h (pf:eq-subst (lam r r) e h))))))
!(set:theorem transport-cited (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))) (pf:known transport))
!(match &self (set:known transport $p $o $f $d $r $s) (DirectDependencies $d))
!(match &self (set:known transport-cited $p $o $f $d $r $s) (NestedDependencies (map-atom $d $entry (car-atom $entry))))
!(pf:rests-on &self transport-cited)
!(let (SetNativeProofV1 $n $p $f $t $ty $c $r $rows $d) (set:native-proof transport) (NativeAssumptions $rows))
!(let $package (set:native-proof transport-cited) (add-atom &self (saved-native $package)))
!(let $package (set:native-proof transport) (let $linked (set:native-link identity $package) (add-atom &self (saved-linked $linked))))
!(car-atom (try (set:proves (pf:known transport-cited) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))))))
!(car-atom (try (set:recheck transport)))
!(car-atom (try (set:native-proof transport-cited)))

; Imported records must preserve every dependency the rechecked proof uses.
; A rule dependency omitted directly, or hidden behind an omitted citation,
; cannot be laundered through recheck. Extra current pins are harmless.
!(match &self (set:known transport $p $o $f $d $r $s) (add-atom &self (set:known stripped-rule $p $o $f () $r $s)))
!(try (set:recheck stripped-rule))
!(set:recheck stripped-rule)
!(match &self (set:known transport-cited $p $o $f $d $r $s) (add-atom &self (set:known stripped-citation $p $o $f () $r $s)))
!(try (set:recheck stripped-citation))
!(set:recheck stripped-citation)
!(match &self (set:known transport $p $o $f $d $r $s) (add-atom &self (set:known wrong-rule-pin $p $o $f (((set:rule eq-subst) (set:rule other))) $r $s)))
!(try (set:recheck wrong-rule-pin))
!(match &self (set:known transport $p $o $f $d $r $s) (let $extra (cons-atom (independent (all prop (lam p (imp p p)))) $d) (add-atom &self (set:known extra-pins $p $o $f $extra $r $s))))
!(match &self (set:known extra-pins $p $o $f $d $r $s) (ExtraDependencies $d))
!(set:recheck extra-pins)
!(car-atom (try (set:proves (pf:known extra-pins) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))))))

; The original signature is unchanged. Only the used rule is withdrawn.
!(remove-atom &self (set:rule eq-subst))
!(try (set:proves (pf:known transport) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (set:proves (pf:known transport-cited) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (set:proves (pf:known extra-pins) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (set:proves (pf:known stripped-rule) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (set:proves (pf:known stripped-citation) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (set:proves (pf:fix p (pf:fix q (pf:assume e (pf:assume h (pf:eq-subst (lam r r) e h))))) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (set:recheck transport-cited))
!(try (set:native-proof transport-cited))
!(match &self (saved-native $package)
   (let (SetNativeProofV1 $n $p $f $t $ty $c $r $rows $d) $package
     (try (set:native-use $package (Lam $ty (idx 0))))))
!(match &self (saved-native $package)
   (let (SetNativeProofV1 $n $p $f $t $ty $c $r $rows $d) $package
     (try (set:native-normalize $package (Lam $ty (idx 0))))))
!(match &self (saved-native $package) (try (set:native-link identity $package)))
!(match &self (saved-linked $package)
   (let (SetNativeLinkedV1 $i $p $t $ty $c $r $rows) $package
     (try (set:native-use $package (Lam $ty (idx 0))))))
!(car-atom (try (set:proves (pf:known independent) (all prop (lam p (imp p p))))))
!(car-atom (try (set:native-proof independent)))

; A diagnostic recheck changes no admission. Restoring the exact rule
; restores those proofs. A failed plain recheck withdraws the receipt until
; a subsequent successful plain recheck.
!(add-atom &self (set:rule eq-subst))
!(car-atom (try (set:proves (pf:known transport-cited) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))))))
!(car-atom (try (set:native-proof transport-cited)))
!(remove-atom &self (set:rule eq-subst))
!(set:recheck transport)
!(add-atom &self (set:rule eq-subst))
!(try (set:proves (pf:known transport) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(try (set:proves (pf:known transport-cited) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))))
!(set:recheck transport)
!(car-atom (try (set:proves (pf:known transport-cited) (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q)))))))))

; Named assumptions have the same completeness requirement. In particular,
; a proof of Falsum from an explicit assumption must not survive removal
; of that assumption by importing a stripped record.
!(set:axiom premise Falsum)
!(set:theorem from-premise Falsum (pf:known premise))
!(match &self (set:known from-premise $p $o $f $d $r $s) (add-atom &self (set:known stripped-premise $p $o $f () $r $s)))
!(try (set:recheck stripped-premise))
!(set:recheck stripped-premise)
!(match &self (set:known premise $p $o $f $d $r $s) (remove-atom &self (set:known premise $p $o $f $d $r $s)))
!(try (set:proves (pf:known from-premise) Falsum))
!(try (set:proves (pf:known stripped-premise) Falsum))
