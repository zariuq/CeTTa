; Proof plans compose by joint completion: three plans that fit pairwise and
; not together.
;
; The calculus has two bits, t and f, and two judgments: Same(x, y), derived
; by sameTT and sameFF, and Diff(x, y), derived by diffTF and diffFT.  So
; Same(x, y) is derivable exactly when x = y, Diff(x, y) exactly when x /= y,
; and every derivable judgment has one derivation (same_derivable_iff,
; diff_derivable_iff, sameDiff_derivation_unique).
;
; A plan network has three components, a, b and c, sharing role fillers x, y
; and z: component a has fillers (x, y), b has (y, z), c has (z, x).  At its
; fillers, each component's plan assumes one obligation: Same(x, y), Same(y, z)
; and Diff(z, x).  A hole is an undeclared head applied to what it depends on,
; as in the round-2 hole fixtures: (? 0) is the plan's obligation 0.  A local
; completion is a choice of fillers with a completion of the plan at them; an
; interface agrees when the second filler of one component is the first of
; the next; a joint completion is a local completion at every component, all
; interfaces agreeing.  Fillers are enumerated t before f.
;
; Where the plans meet, the local completions become views: the view of a
; local completion is its first filler, and it determines the completion.
; Crossing an interface translates the view of a completion into the view of
; the only completion on the other side that agrees with it.  Transport
; around the cycle a -> b -> c -> a is the holonomy.  Composing the plans
; looks for a view at a that the cycle fixes; if there is none, the error
; names the cycle and its holonomy; otherwise the view is carried along
; a -> b -> c, turned back into completions, and the result is checked
; against every interface.  With Same(z, x) in place of Diff(z, x) the plans
; compose.
;
; This fixture composes plans: obligations, their completions in a derivation
; calculus, and translations computed from those completions.  Constraint
; networks over bare values are the pairwise_not_joint fixture of T1, and
; bare view systems the twisted_cycle fixture of W11.
;
; Mirrors: Triangle.pairwise, Triangle.no_joint, Triangle.pairwise_not_joint,
; Triangle.viewSystem_eq, Triangle.holonomy_is_negation,
; Triangle.no_joint_by_holonomy, jointCompletion_nonempty_iff,
; InvertibleInterfaces.jointCompletion_nonempty_iff_invariant,
; Untwisted.joint.
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

; ---- the calculus ------------------------------------------------------------------

(rule sameTT () (Same t t))
(rule sameFF () (Same f f))
(rule diffTF () (Diff t f))
(rule diffFT () (Diff f t))

(= (derivations-of $j) (collapse (match &self (rule $r () $j) $r)))

; ---- the plan networks ----------------------------------------------------------------

(= (bits) (t f))
(= (edges) ((ab a b) (bc b c) (ca c a)))
(= (goal twisted a ($x $y)) (Same $x $y))
(= (goal twisted b ($x $y)) (Same $x $y))
(= (goal twisted c ($x $y)) (Diff $x $y))
(= (goal untwisted $i ($x $y)) (Same $x $y))

; The plan of a component at its fillers: it assumes its obligation.
(= (plan-at $net $i $fillers) (let $j (goal $net $i $fillers) (plan ($j) (? 0))))
; Completing a plan that assumes J with a derivation of J.
(= (completions-of (plan ($j) (? 0))) (derivations-of $j))
(= (local-completions $net $i)
   (let $bs (bits)
     (collapse (let* (($x (superpose $bs)) ($y (superpose $bs))
                      ($p (plan-at $net $i ($x $y)))
                      ($ds (completions-of $p)) ($d (superpose $ds)))
                 (($x $y) $d)))))

(= (agree (($x1 $y1) $d1) (($x2 $y2) $d2)) (== $y1 $x2))
(= (edge-agrees $net ($e $i $j))
   (let* (($li (local-completions $net $i)) ($lj (local-completions $net $j))
          ($hits (collapse (let* (($l1 (superpose $li)) ($l2 (superpose $lj)))
                             (if (agree $l1 $l2) ($l1 $l2) (empty))))))
     (not (== $hits ()))))
(= (joint-completions $net)
   (let* (($la (local-completions $net a)) ($lb (local-completions $net b))
          ($lc (local-completions $net c)))
     (collapse (let* (($x (superpose $la)) ($y (superpose $lb)) ($z (superpose $lc)))
                 (if (and (agree $x $y) (and (agree $y $z) (agree $z $x)))
                     ((a $x) (b $y) (c $z)) (empty))))))

; ---- views, translations and holonomy ----------------------------------------------

(= (view (($x $y) $d)) $x)
(= (with-view $net $i $v)
   (let $ls (local-completions $net $i)
     (collapse (let $l (superpose $ls) (if (== (view $l) $v) $l (empty))))))
(= (edge-named $e)
   (let* (($es (edges))
          ($hits (collapse (let ($n $i $j) (superpose $es) (if (== $n $e) ($n $i $j) (empty))))))
     (car-atom $hits)))
; The view on the far side of an interface, from the only agreeing completion.
(= (translate $net $e $v)
   (let* ((($n $i $j) (edge-named $e))
          ($from (with-view $net $i $v)) ($lj (local-completions $net $j))
          ($to (collapse (let* (($l1 (superpose $from)) ($l2 (superpose $lj)))
                           (if (agree $l1 $l2) (view $l2) (empty))))))
     (if (== (size-atom $to) 1) (car-atom $to) (not-invertible $e $v))))
(= (transport $net $walk $v) (foldl-atom $walk $v $w $e (translate $net $e $w)))
(= (translation-table $net $e)
   (let $bs (bits) (map-atom $bs $v (let $w (translate $net $e $v) ($v $w)))))

; ---- composing the plans --------------------------------------------------------------

(= (cycle-walk) (ab bc ca))
(= (compose $net)
   (let* (($walk (cycle-walk)) ($bs (bits))
          ($hol (map-atom $bs $v (let $w (transport $net $walk $v) ($v $w))))
          ($fixed (collapse (let ($v $w) (superpose $hol) (if (== $v $w) $v (empty))))))
     (if (== $fixed ())
         (Refuted (no-joint-completion (cycle a $walk) (holonomy $hol)))
         (let* (($va (car-atom $fixed))
                ($vb (transport $net (ab) $va)) ($vc (transport $net (ab bc) $va))
                ($as (with-view $net a $va)) ($bs2 (with-view $net b $vb)) ($cs (with-view $net c $vc))
                ($la (car-atom $as)) ($lb (car-atom $bs2)) ($lc (car-atom $cs)))
           (if (and (agree $la $lb) (and (agree $lb $lc) (agree $lc $la)))
               (Established ((a $la) (b $lb) (c $lc)))
               (Refuted (interfaces-disagree ((a $la) (b $lb) (c $lc)))))))))

; ---- the triangle -----------------------------------------------------------------------

; 1. The completions of component c: its plan at fillers (z, x) completes with
;    diffTF or diffFT, one derivation each (diff_derivable_iff,
;    sameDiff_derivation_unique).
!(local-completions twisted c)
; 2. Every interface can be satisfied on its own (Triangle.pairwise) ...
!(let $es (edges) (map-atom $es $e (edge-agrees twisted $e)))
; 3. ... and no joint completion exists (Triangle.no_joint,
;    Triangle.pairwise_not_joint).
!(let $js (joint-completions twisted) (size-atom $js))
; 4. The translations computed from the completions are those of the twisted
;    three-cycle: identity, identity, negation (Triangle.viewSystem_eq).
!(let $es (cycle-walk) (map-atom $es $e (let $tab (translation-table twisted $e) ($e $tab))))
; 5. Around the cycle, t comes back f (Triangle.holonomy_is_negation).
!(let $walk (cycle-walk) (transport twisted $walk t))
; 6. Composition fails, and the error names the cycle and its holonomy, which
;    moves every view (Triangle.no_joint_by_holonomy,
;    jointCompletion_nonempty_iff_invariant).
!(compose twisted)
; 7. The two routes agree: exhaustive search finds no joint completion, and no
;    view at a is fixed by the cycle (Triangle.no_joint,
;    Triangle.no_joint_by_holonomy).
!(let* (($js (joint-completions twisted)) ($walk (cycle-walk))
        ($fixed (collapse (let $v (superpose (t f)) (if (== (transport twisted $walk $v) $v) $v (empty))))))
   ((== $js ()) (== $fixed ())))

; ---- the untwisted triangle ---------------------------------------------------------

; 8. With Same(z, x) at c the plans compose: every filler t, every obligation
;    closed by sameTT (Untwisted.joint).
!(compose untwisted)
!(let $js (joint-completions untwisted) (not (== $js ())))
