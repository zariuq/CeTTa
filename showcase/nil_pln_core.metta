; Nil Geisweiller's dependent-PLN wishes, ported to the he-prime checked typer.
; Proof constructors are ordinary typed MeTTa terms.  Formula functions are
; explicitly admitted for bounded computation inside dependent codomains.

(: TruthValue Type)
(: STV (-> Number Number TruthValue))
(: OrderedSet (-> $a Type))
(: ENil (OrderedSet $a))
(: ECons (-> $a (OrderedSet $a) (OrderedSet $a)))
(: EvidentialTruthValue Type)
(: ETV (-> (OrderedSet $a) TruthValue EvidentialTruthValue))

(: → (-> Atom Atom Atom))
(: ≞ (-> $event $measure Type))
(: ⊷ (-> Atom Atom Bool Type))

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: ⍃ (-> Nat Nat Type))

(: pln-min (-> Number Number Number))
(= (pln-min $x $y) (if (< $x $y) $x $y))
(: pln-max (-> Number Number Number))
(= (pln-max $x $y) (if (> $x $y) $x $y))
(: pln-clamp (-> Number Number Number Number))
(= (pln-clamp $x $lo $hi) (pln-max $lo (pln-min $hi $x)))
(: pln-abs (-> Number Number))
(= (pln-abs $x) (if (< $x 0) (* -1 $x) $x))
(: pln-approx-eq (-> Number Number Number Bool))
(= (pln-approx-eq $x $y $eps) (<= (pln-abs (- $x $y)) $eps))
(: count->confidence (-> Number Number))
(= (count->confidence $cnt) (/ $cnt (+ $cnt 1.0)))
(: confidence->count (-> Number Number))
(= (confidence->count $conf)
   (if (pln-approx-eq 1.0 $conf 0.000000001)
       1000000000.0
       (/ $conf (- 1.0 $conf))))
(: bool->number (-> Bool Number))
(= (bool->number False) 0.0)
(= (bool->number True) 1.0)

(: inc-neg-count (-> TruthValue TruthValue))
(= (inc-neg-count (STV $s $c))
   (let* (($tot (confidence->count $c))
          ($pos (* $s $tot))
          ($next (+ $tot 1.0)))
     (STV (/ $pos $next) (count->confidence $next))))
(: inc-pos-count (-> TruthValue TruthValue))
(= (inc-pos-count (STV $s $c))
   (let* (($tot (confidence->count $c))
          ($pos (* $s $tot))
          ($next-pos (+ $pos 1.0))
          ($next (+ $tot 1.0)))
     (STV (/ $next-pos $next) (count->confidence $next))))

(: smallest-intersection-probability (-> Number Number Number))
(= (smallest-intersection-probability $As $Bs)
   (pln-clamp (/ (- (+ $As $Bs) 1.0) $As) 0.0 1.0))
(: largest-intersection-probability (-> Number Number Number))
(= (largest-intersection-probability $As $Bs)
   (pln-clamp (/ $Bs $As) 0.0 1.0))
(: conditional-probability-consistency (-> Number Number Number Bool))
(= (conditional-probability-consistency $As $Bs $ABs)
   (and (< 0.0 $As)
        (and (<= (smallest-intersection-probability $As $Bs) $ABs)
             (<= $ABs (largest-intersection-probability $As $Bs)))))

(: deduction-formula (-> TruthValue TruthValue TruthValue TruthValue TruthValue TruthValue))
(= (deduction-formula (STV $Ps $Pc)
                      (STV $Qs $Qc)
                      (STV $Rs $Rc)
                      (STV $PQs $PQc)
                      (STV $QRs $QRc))
   (if (and (conditional-probability-consistency $Ps $Qs $PQs)
            (conditional-probability-consistency $Qs $Rs $QRs))
       (STV (if (< 0.9999 $Qs)
                $Rs
                (+ (* $PQs $QRs)
                   (/ (* (- 1.0 $PQs) (- $Rs (* $Qs $QRs)))
                      (- 1.0 $Qs))))
            (pln-min $Pc (pln-min $Qc (pln-min $Rc
              (pln-min $PQc $QRc)))))
       (STV 1.0 0.0)))
(type-level-function deduction-formula)

(: modus-ponens-formula (-> TruthValue TruthValue TruthValue))
(= (modus-ponens-formula (STV $As $Ac) (STV $ABs $ABc))
   (STV (+ (* $ABs $As) (* 0.2 (- 1.0 $As)))
        (pln-min $ABc $Ac)))
(type-level-function modus-ponens-formula)

(: direct-introduction-base-formula (-> Bool TruthValue))
(= (direct-introduction-base-formula $qa)
   (STV (bool->number $qa) (count->confidence 1.0)))
(type-level-function direct-introduction-base-formula)
(: direct-introduction-recursive-formula (-> Bool TruthValue TruthValue))
(= (direct-introduction-recursive-formula False $tv) (inc-neg-count $tv))
(= (direct-introduction-recursive-formula True $tv) (inc-pos-count $tv))
(type-level-function direct-introduction-recursive-formula)

(: Deduction
   (-> (≞ $p $ptv)
       (≞ $q $qtv)
       (≞ $r $rtv)
       (≞ (→ $p $q) $pqtv)
       (≞ (→ $q $r) $qrtv)
       (≞ (→ $p $r)
          (deduction-formula $ptv $qtv $rtv $pqtv $qrtv))))
(chaining-rule Deduction)

(: ModusPonens
   (-> (≞ $a $atv)
       (≞ (→ $a $b) $abtv)
       (≞ $b (modus-ponens-formula $atv $abtv))))
(chaining-rule ModusPonens)

(: IDIBase
   (-> (⊷ $p $a True)
       (⊷ $q $a $qa)
       (≞ (→ $p $q)
          (ETV (ECons $a ENil)
               (direct-introduction-base-formula $qa)))))
(chaining-rule IDIBase)

(: IDIRecursive
   (-> (⊷ $p $a True)
       (⊷ $q $a $qa)
       (≞ (→ $p $q) (ETV (ECons $h $t) $pqtv))
       (⍃ $h $a)
       (≞ (→ $p $q)
          (ETV (ECons $a (ECons $h $t))
               (direct-introduction-recursive-formula $qa $pqtv)))))
(chaining-rule IDIRecursive)

(: EvidenceElimination
   (-> (≞ $event (ETV $evidence $tv))
       (≞ $event $tv)))
(chaining-rule EvidenceElimination)

(: ZeroLTSucc (⍃ Z (S $k)))
(: SuccMonotonicity
   (-> (: $order-proof (⍃ $x $y))
       (⍃ (S $x) (S $y))))
(chaining-rule SuccMonotonicity)

; A generic checked natural-index refinement.  This closes the VecN underflow
; that both native HE and the old dependent-shaped computation admitted.
(: natural-index (-> Number Bool))
(= (natural-index $n) (>= $n 0))
(type-level-function natural-index)
(: VecN (-> Atom Number Type))
(type-index-refinement VecN 2 natural-index)
