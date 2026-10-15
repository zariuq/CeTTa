; =============================================================================
; kernel_binding_waist_v1.metta
; Vendored from the MeTTapedia proof-kernel curriculum (MettaKernel/kernel, commit 1d882c9f0).
;
; Minimal performant binding waist v1.
;
; Source anchors: de Bruijn shift/substitution and capture-avoidance are
; calibrated to the existing BIND-* shelf (POPLmark, Chargueraud locally
; nameless, Autosubst, Gabbay-Pitts) and to PFPL/ABT presentations for binding
; declarations.  This file cross-references those ledger rows rather than
; duplicating their literature trail.
;
; Implementation notes, not source claims: a small import-free arity table and
; evaluator-stable fast path for the active LF/DIndG/NatRec term constructors.
;
; Import-free ABT/de-Bruijn binding core: owns the current binding arity table
; and generic capture-avoiding shift/subst traversal.  It deliberately contains
; no checker, normalizer, conversion, signature lookup, iota, or DIndG typing
; rules.  Kernel files and LanguageDef notation files can import this small waist
; without pulling in the full indexed checker.
; =============================================================================

; ---------- binding arity declarations ---------------------------------------
(= (bind-arity AppF 0) 0)
(= (bind-arity AppF 1) 0)
(= (bind-arity PiF  0) 0)
(= (bind-arity PiF  1) 1)
(= (bind-arity LamF 0) 0)
(= (bind-arity LamF 1) 1)
(= (bind-arity ArgsF 0) 0)
(= (bind-arity CasesF 0) 0)
(= (bind-arity NatRecF 0) 0)
(= (bind-arity NatRecF 1) 0)
(= (bind-arity NatRecF 2) 0)
(= (bind-arity NatRecF 3) 0)

; The waist owns and checks the arity table.
!(assertEqual (bind-arity AppF 0) 0)
!(assertEqual (bind-arity AppF 1) 0)
!(assertEqual (bind-arity PiF 0) 0)
!(assertEqual (bind-arity PiF 1) 1)
!(assertEqual (bind-arity LamF 0) 0)
!(assertEqual (bind-arity LamF 1) 1)
!(assertEqual (bind-arity ArgsF 0) 0)
!(assertEqual (bind-arity CasesF 0) 0)
!(assertEqual (bind-arity NatRecF 0) 0)
!(assertEqual (bind-arity NatRecF 1) 0)
!(assertEqual (bind-arity NatRecF 2) 0)
!(assertEqual (bind-arity NatRecF 3) 0)

; ---------- arity-driven child traversal -------------------------------------
(= (bindg-shift-under $d $c 0 $t) (bind-shift $d $c $t))
(= (bindg-shift-under $d $c 1 $t)
   (let $c1 (+ $c 1) (bind-shift $d $c1 $t)))

(= (bindg-subst-under $j $s 0 $t) (bind-subst $j $s $t))
(= (bindg-subst-under $j $s 1 $t)
   (let $j1 (+ $j 1)
     (let $s1 (bind-shift 1 0 $s)
       (bind-subst $j1 $s1 $t))))

(= (bind-mk2 AppF $x $y) (App $x $y))
(= (bind-mk2 PiF  $x $y) (Pi  $x $y))
(= (bind-mk2 LamF $x $y) (Lam $x $y))

(= (bind-shift2 $tag $d $c $b1 $t1 $b2 $t2)
   (let $u1 (bindg-shift-under $d $c $b1 $t1)
     (let $u2 (bindg-shift-under $d $c $b2 $t2)
       (bind-mk2 $tag $u1 $u2))))
(= (bind-subst2 $tag $j $s $b1 $t1 $b2 $t2)
   (let $u1 (bindg-subst-under $j $s $b1 $t1)
     (let $u2 (bindg-subst-under $j $s $b2 $t2)
       (bind-mk2 $tag $u1 $u2))))

; ---------- generic term traversal ------------------------------------------
(= (bind-shift $d $c (Var $k)) (if (< $k $c) (Var $k) (Var (+ $k $d))))
(= (bind-shift $d $c (Srt $s)) (Srt $s))
(= (bind-shift $d $c (Con $x)) (Con $x))
(= (bind-shift $d $c (Def $x)) (Def $x))
(= (bind-shift $d $c (Bad $e)) (Bad $e))
(= (bind-shift $d $c (App $f $a))
   (let $b0 (bind-arity AppF 0)
     (let $b1 (bind-arity AppF 1)
       (bind-shift2 AppF $d $c $b0 $f $b1 $a))))
(= (bind-shift $d $c (Pi  $A $B))
   (let $b0 (bind-arity PiF 0)
     (let $b1 (bind-arity PiF 1)
       (bind-shift2 PiF $d $c $b0 $A $b1 $B))))
(= (bind-shift $d $c (Lam $A $b))
   (let $b0 (bind-arity LamF 0)
     (let $b1 (bind-arity LamF 1)
       (bind-shift2 LamF $d $c $b0 $A $b1 $b))))
(= (bind-shift $d $c (IndG $name $params $motive $cases $indices $scrut))
   (IndG $name
     (bind-shift-args $d $c $params)
     (bind-shift $d $c $motive)
     (bind-shift-cases $d $c $cases)
     (bind-shift-args $d $c $indices)
     (bind-shift $d $c $scrut)))
(= (bind-shift $d $c (NatRec $P $zcase $scase $n))
   (let $b0 (bind-arity NatRecF 0)
     (let $b1 (bind-arity NatRecF 1)
       (let $b2 (bind-arity NatRecF 2)
         (let $b3 (bind-arity NatRecF 3)
           (NatRec
             (bindg-shift-under $d $c $b0 $P)
             (bindg-shift-under $d $c $b1 $zcase)
             (bindg-shift-under $d $c $b2 $scase)
             (bindg-shift-under $d $c $b3 $n)))))))

(= (bind-subst $j $s (Var $k)) (if (== $k $j) $s (if (> $k $j) (Var (- $k 1)) (Var $k))))
(= (bind-subst $j $s (Srt $x)) (Srt $x))
(= (bind-subst $j $s (Con $x)) (Con $x))
(= (bind-subst $j $s (Def $x)) (Def $x))
(= (bind-subst $j $s (Bad $e)) (Bad $e))
(= (bind-subst $j $s (App $f $a))
   (let $b0 (bind-arity AppF 0)
     (let $b1 (bind-arity AppF 1)
       (bind-subst2 AppF $j $s $b0 $f $b1 $a))))
(= (bind-subst $j $s (Pi  $A $B))
   (let $b0 (bind-arity PiF 0)
     (let $b1 (bind-arity PiF 1)
       (bind-subst2 PiF $j $s $b0 $A $b1 $B))))
(= (bind-subst $j $s (Lam $A $b))
   (let $b0 (bind-arity LamF 0)
     (let $b1 (bind-arity LamF 1)
       (bind-subst2 LamF $j $s $b0 $A $b1 $b))))
(= (bind-subst $j $s (IndG $name $params $motive $cases $indices $scrut))
   (IndG $name
     (bind-subst-args $j $s $params)
     (bind-subst $j $s $motive)
     (bind-subst-cases $j $s $cases)
     (bind-subst-args $j $s $indices)
     (bind-subst $j $s $scrut)))
(= (bind-subst $j $s (NatRec $P $zcase $scase $n))
   (let $b0 (bind-arity NatRecF 0)
     (let $b1 (bind-arity NatRecF 1)
       (let $b2 (bind-arity NatRecF 2)
         (let $b3 (bind-arity NatRecF 3)
           (NatRec
             (bindg-subst-under $j $s $b0 $P)
             (bindg-subst-under $j $s $b1 $zcase)
             (bindg-subst-under $j $s $b2 $scase)
             (bindg-subst-under $j $s $b3 $n)))))))

!(assertEqual
  (bindg-shift-under 1 0 0 (Var 0))
  (Var 1))
!(assertEqual
  (bindg-shift-under 1 0 1 (Var 0))
  (Var 0))
!(assertEqual
  (bindg-shift-under 1 0 0 (Var 1))
  (Var 2))
!(assertEqual
  (bindg-subst-under 0 (Con z) 1 (Var 1))
  (Con z))

; ---------- generic container traversal --------------------------------------
(= (bind-shift-args $d $c ArgsNil) ArgsNil)
(= (bind-shift-args $d $c (ArgsCons $a $r))
   (let $b0 (bind-arity ArgsF 0)
     (ArgsCons (bindg-shift-under $d $c $b0 $a) (bind-shift-args $d $c $r))))
(= (bind-subst-args $j $s ArgsNil) ArgsNil)
(= (bind-subst-args $j $s (ArgsCons $a $r))
   (let $b0 (bind-arity ArgsF 0)
     (ArgsCons (bindg-subst-under $j $s $b0 $a) (bind-subst-args $j $s $r))))
(= (bind-shift-cases $d $c CasesNil) CasesNil)
(= (bind-shift-cases $d $c (Cases (Case $cn $body) $rest))
   (let $b0 (bind-arity CasesF 0)
     (Cases (Case $cn (bindg-shift-under $d $c $b0 $body)) (bind-shift-cases $d $c $rest))))
(= (bind-subst-cases $j $s CasesNil) CasesNil)
(= (bind-subst-cases $j $s (Cases (Case $cn $body) $rest))
   (let $b0 (bind-arity CasesF 0)
     (Cases (Case $cn (bindg-subst-under $j $s $b0 $body)) (bind-subst-cases $j $s $rest))))

; ---------- public compatibility fast path -----------------------------------
; Clients call `shift`/`subst`; this waist owns the evaluator-stable public
; dispatch.  Keeping these per-constructor clauses here preserves the fast path
; while preventing each kernel or LanguageDef client from reimplementing binder
; traversal.
(= (shift $d $c (Var $k)) (bind-shift $d $c (Var $k)))
(= (shift $d $c (Srt $s)) (bind-shift $d $c (Srt $s)))
(= (shift $d $c (Con $x)) (bind-shift $d $c (Con $x)))
(= (shift $d $c (Def $x)) (bind-shift $d $c (Def $x)))
(= (shift $d $c (App $f $a)) (bind-shift $d $c (App $f $a)))
(= (shift $d $c (Pi  $A $B)) (bind-shift $d $c (Pi $A $B)))
(= (shift $d $c (Lam $A $b)) (bind-shift $d $c (Lam $A $b)))
(= (shift $d $c (Bad $e)) (bind-shift $d $c (Bad $e)))
(= (shift $d $c (IndG $name $params $motive $cases $indices $scrut))
   (bind-shift $d $c (IndG $name $params $motive $cases $indices $scrut)))
(= (shift $d $c (NatRec $P $zcase $scase $n))
   (bind-shift $d $c (NatRec $P $zcase $scase $n)))

(= (subst $j $s (Var $k)) (bind-subst $j $s (Var $k)))
(= (subst $j $s (Srt $x)) (bind-subst $j $s (Srt $x)))
(= (subst $j $s (Con $x)) (bind-subst $j $s (Con $x)))
(= (subst $j $s (Def $x)) (bind-subst $j $s (Def $x)))
(= (subst $j $s (App $f $a)) (bind-subst $j $s (App $f $a)))
(= (subst $j $s (Pi  $A $B)) (bind-subst $j $s (Pi $A $B)))
(= (subst $j $s (Lam $A $b)) (bind-subst $j $s (Lam $A $b)))
(= (subst $j $s (Bad $e)) (bind-subst $j $s (Bad $e)))
(= (subst $j $s (IndG $name $params $motive $cases $indices $scrut))
   (bind-subst $j $s (IndG $name $params $motive $cases $indices $scrut)))
(= (subst $j $s (NatRec $P $zcase $scase $n))
   (bind-subst $j $s (NatRec $P $zcase $scase $n)))

(= (shift-args $d $c ArgsNil) (bind-shift-args $d $c ArgsNil))
(= (shift-args $d $c (ArgsCons $a $r)) (bind-shift-args $d $c (ArgsCons $a $r)))
(= (subst-args $j $s ArgsNil) (bind-subst-args $j $s ArgsNil))
(= (subst-args $j $s (ArgsCons $a $r)) (bind-subst-args $j $s (ArgsCons $a $r)))
(= (shift-cases $d $c CasesNil) (bind-shift-cases $d $c CasesNil))
(= (shift-cases $d $c (Cases (Case $cn $body) $rest))
   (bind-shift-cases $d $c (Cases (Case $cn $body) $rest)))
(= (subst-cases $j $s CasesNil) (bind-subst-cases $j $s CasesNil))
(= (subst-cases $j $s (Cases (Case $cn $body) $rest))
   (bind-subst-cases $j $s (Cases (Case $cn $body) $rest)))
