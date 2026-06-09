; he-prime DTT tutorial ladder.
; These are small tutorial-style exercises translated into MeTTa surface forms.

; EXERCISE: Bool and Nat are ordinary indexed-family foundations.
(: TutBool Type)
(: TutTrue TutBool)
(: TutFalse TutBool)
(: TutNat Type)
(: TutZ TutNat)
(: TutS (-> TutNat TutNat))

!(assertEqual (get-type TutTrue) TutBool)
!(assertEqual (get-type (TutS TutZ)) TutNat)

; EXERCISE: Fin indexes bounded positions. There is no inhabitant of
; (TutFin TutZ), so safe indexing never needs an empty-vector branch.
(: TutFin (-> TutNat Type))
(: TutFZ (TutFin (TutS $n)))
(: TutFS (-> (TutFin $n) (TutFin (TutS $n))))
(: TutVec (-> $t TutNat Type))
(: TutNil (TutVec $t TutZ))
(: TutCons (-> $t (TutVec $t $n) (TutVec $t (TutS $n))))
(: TutIndex (-> (: $i (TutFin $n)) (: $xs (TutVec $t $n)) $t))
(= (TutIndex TutFZ (TutCons $x $xs)) $x)
(= (TutIndex (TutFS $i) (TutCons $x $xs)) (TutIndex $i $xs))

!(assertEqual
  (get-type (TutIndex TutFZ (TutCons "a" TutNil)))
  String)
!(assertEqual
  (TutIndex TutFZ (TutCons "a" TutNil))
  "a")
!(assertEqualToResult
  (get-type (TutIndex TutFZ TutNil))
  ())

; EXERCISE: equality has reflexivity and congruence for a concrete function.
(: TutEq (-> $t $x $y Type))
(: TutRefl (-> (: $x $t) (TutEq $t $x $x)))
(: TutSuccCong (-> (: $p (TutEq TutNat $x $y))
                   (TutEq TutNat (TutS $x) (TutS $y))))

!(assertEqual
  (get-type (TutRefl TutZ))
  (TutEq TutNat TutZ TutZ))
!(assertEqual
  (get-type (TutSuccCong (TutRefl TutZ)))
  (TutEq TutNat (TutS TutZ) (TutS TutZ)))
!(assertEqualToResult
  (get-type (TutSuccCong TutZ))
  ())

; EXERCISE: dependent pairs package a value with a proof about that value.
(: TutEntity Type)
(: TutAlice TutEntity)
(: TutBob TutEntity)
(: TutHuman (-> TutEntity Type))
(: TutHumanAlice (TutHuman TutAlice))
(: TutSigmaHuman Type)
(: TutPackHuman (-> (: $x TutEntity)
                    (: $p (TutHuman $x))
                    TutSigmaHuman))

!(assertEqual
  (get-type (TutPackHuman TutAlice TutHumanAlice))
  TutSigmaHuman)
!(assertEqualToResult
  (get-type (TutPackHuman TutBob TutHumanAlice))
  ())

; EXERCISE: dependent type families can express small security capabilities.
(: TutSecret Type)
(: TutDoc TutSecret)
(: TutCanRead (-> TutEntity TutSecret Type))
(: TutReadOK (-> TutEntity TutSecret Type))
(: TutAliceDocCap (TutCanRead TutAlice TutDoc))
(: TutRead (-> (: $who TutEntity)
               (: $doc TutSecret)
               (: $cap (TutCanRead $who $doc))
               (TutReadOK $who $doc)))

!(assertEqual
  (get-type (TutRead TutAlice TutDoc TutAliceDocCap))
  (TutReadOK TutAlice TutDoc))
!(assertEqualToResult
  (get-type (TutRead TutBob TutDoc TutAliceDocCap))
  ())

; PUREKERNEL-ALIGNED EXERCISE: this surface Unit recursor mirrors the current
; PureKernel Unit.rec iota witness without adding a runtime trace surface.
(: TutUnit Type)
(: TutUnitCtor TutUnit)
(: TutUnitRecSurface (-> (: $R Type) $R TutUnit $R))
(= (TutUnitRecSurface $R $base TutUnitCtor) $base)

!(assertEqual
  (get-type (TutUnitRecSurface TutUnit TutUnitCtor TutUnitCtor))
  TutUnit)
!(assertEqual
  (TutUnitRecSurface TutUnit TutUnitCtor TutUnitCtor)
  TutUnitCtor)
!(assertEqualToResult
  (get-type (TutUnitRecSurface TutUnit TutTrue TutUnitCtor))
  ())

; OPEN EXERCISE: recursors/eliminators are the next tutorial fork. The current
; corpus can declare their types, but reducing through them needs a principled
; PureKernel/MeTTa bridge rather than ad hoc runtime equations.
(: TutNatRec (-> (: $P (-> TutNat Type))
                 ($P TutZ)
                 (: $step (-> (: $n TutNat) ($P $n) ($P (TutS $n))))
                 (: $n TutNat)
                 ($P $n)))
!(assertAlphaEqual
  (get-type TutNatRec)
  (-> (: $P (-> TutNat Type)) ($P TutZ) (: $step (-> (: $n TutNat) ($P $n) ($P (TutS $n)))) (: $n TutNat) ($P $n)))

; RUNTIME SURFACE: ordinary MeTTa equations can run a non-dependent Nat
; recursor-shaped program today. This is design pressure for the real kernel
; recursor, not the trusted kernel primitive itself.
(: TutNatRecSurface (-> $t (-> TutNat $t $t) TutNat $t))
(: TutNatRecStepS (-> TutNat TutNat TutNat))
(: TutBadNatRecStep (-> TutBool TutNat TutNat))
(= (TutNatRecStepS $n $acc) (TutS $acc))
(= (TutNatRecSurface $z $step TutZ) $z)
(= (TutNatRecSurface $z $step (TutS $n))
   ($step $n (TutNatRecSurface $z $step $n)))

!(assertEqual
  (get-type (TutNatRecSurface TutZ TutNatRecStepS (TutS TutZ)))
  TutNat)
!(assertEqual
  (TutNatRecSurface TutZ TutNatRecStepS (TutS (TutS TutZ)))
  (TutS (TutS TutZ)))
!(assertEqualToResult
  (get-type (TutNatRecSurface TutZ TutBadNatRecStep (TutS TutZ)))
  ())
