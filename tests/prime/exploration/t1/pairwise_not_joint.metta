(set:profile hol)

; Composing components along interfaces.  Each component has local choices,
; and each interface relates the choices of two components.  A joint
; completion is a choice for every component that satisfies every interface.
; Every interface can be satisfiable on its own while no joint completion
; exists: checking interfaces one pair at a time is not enough.
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

; ---- Part 1: networks, searched exhaustively -------------------------------

; The triangle: a = b, b = c and c /= a, over booleans.
(= (choice triangle) (superpose (True False)))
(= (interfaces triangle) ((equal a b) (equal b c) (unequal c a)))
; The chain a - b - c: the first interface wants b false, the second b true.
(= (choice chain) (superpose (True False)))
(= (interfaces chain) ((b-false a b) (b-true b c)))
; The triangle over three points, where /= is not a bijection.
(= (choice three-points) (superpose (0 1 2)))
(= (interfaces three-points) ((equal a b) (equal b c) (unequal c a)))
; The triangle without its last interface.
(= (choice path) (superpose (True False)))
(= (interfaces path) ((equal a b) (equal b c)))

(= (holds equal $x $y) (== $x $y))
(= (holds unequal $x $y) (not (== $x $y)))
(= (holds b-false $x $y) (== $y False))
(= (holds b-true $x $y) (== $x True))

; An interface is satisfiable on its own when some two choices satisfy it.
(= (satisfiable $net $relation)
   (let $witnesses (collapse (let* (($x (choice $net)) ($y (choice $net)))
                               (if (holds $relation $x $y) ($x $y) (empty))))
     (not (== $witnesses ()))))
; Pairwise: every interface is satisfiable on its own.
(= (pairwise $net)
   (let* (($ifaces (interfaces $net))
          ($bad (collapse (let ($relation $i $j) (superpose $ifaces)
                            (if (satisfiable $net $relation) (empty) $relation)))))
     (== $bad ())))

; A joint completion: a choice at a, b and c satisfying every interface.
(= (value a ($va $vb $vc)) $va)
(= (value b ($va $vb $vc)) $vb)
(= (value c ($va $vb $vc)) $vc)
(= (satisfies $x $ifaces)
   (if (== $ifaces ()) True
       (let ($relation $i $j) (car-atom $ifaces)
         (let $rest (cdr-atom $ifaces)
           (and (holds $relation (value $i $x) (value $j $x))
                (satisfies $x $rest))))))
(= (joint $net)
   (let* (($va (choice $net)) ($vb (choice $net)) ($vc (choice $net))
          ($x ($va $vb $vc)) ($ifaces (interfaces $net)))
     (if (satisfies $x $ifaces) $x (empty))))
(= (has-joint $net) (let $all (collapse (joint $net)) (not (== $all ()))))

; 1. The triangle: every interface is satisfiable, and there is no joint
;    completion (Triangle.pairwise_not_joint).
!(pairwise triangle)
!(has-joint triangle)
; 2. The chain has no cycle and the same failure (Chain.pairwise_not_joint).
!(pairwise chain)
!(has-joint chain)
; 3. Over three points, where /= is not a bijection, each relation is
;    inhabited and the cycle still has no joint completion
;    (RelationalTriangle.pairwise, RelationalTriangle.no_joint).
!(pairwise three-points)
!(has-joint three-points)
; 4. Without its last interface the triangle has joint completions
;    (Twisted.path_glues), and a network with a joint completion is pairwise
;    (pairwise_of_joint).
!(has-joint path)
!(pairwise path)

; ---- Part 2: the chain's interfaces as a theory ----------------------------

; A joint completion exists exactly when the theory of the interfaces is
; consistent (jointCompletion_iff_consistent).  For the chain the checker
; derives falsum from the two interfaces, while each alone has a model.

!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &thy bool (u 0) (: true bool) (: false bool))
!(pf:equality &thy bool refl@bool subst@bool)
!(set:define &thy truth prop (= truth (all prop (lam p (imp p p)))))
!(set:define &thy falsum prop (= falsum (all prop (lam p p))))
!(set:define &thy is-true (-> bool prop) (= (is-true true) truth) (= (is-true false) falsum))

; 5. The two interfaces together are inconsistent (Chain.pairwise_not_joint).
!(pf:theorem &thy chain-inconsistent
   (all bool (lam b (imp (eq bool b false) (imp (eq bool b true) falsum))))
   (pf:fix b (pf:assume h0 (pf:assume h1
     (pf:subst-with bool (lam z (is-true z)) true false
       (pf:trans bool true b false (pf:sym bool b true h1) h0)
       (pf:fix p (pf:assume hp hp)))))))
; 6. The first interface alone has a model, b = false (Chain.pairwise_not_joint).
!(pf:theorem &thy first-interface-model
   (all prop (lam r (imp (all bool (lam b (imp (eq bool b false) r))) r)))
   (pf:fix r (pf:assume k (pf:imp-elim (pf:all-elim k false) (pf:refl bool false)))))
