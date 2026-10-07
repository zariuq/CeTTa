(set:profile hol)

; Views glued around a cycle.  Three places a, b and c each hold a view.
; Crossing an edge translates the view.  In the twisted cycle the views are
; booleans, unchanged from a to b and from b to c and negated from c back to
; a: every translation is a bijection, and still the views do not glue.  The
; obstruction is the holonomy, what a trip around the cycle does to a value.
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

; ---- Part 1: computing with the views --------------------------------------

(= (translate twisted ab $v) $v)
(= (translate twisted bc $v) $v)
(= (translate twisted ca $v) (not $v))
; The path a -> b -> c: the twisted cycle without its twisted edge.
(= (translate path ab $v) $v)
(= (translate path bc $v) $v)
; Three points, with the edge from c to a swapping 1 and 2 and fixing 0.
(= (translate swapped ab $v) $v)
(= (translate swapped bc $v) $v)
(= (translate swapped ca $v) (if (== $v 0) 0 (if (== $v 1) 2 1)))

(= (edges twisted) ((ab a b) (bc b c) (ca c a)))
(= (edges path) ((ab a b) (bc b c)))
(= (edges swapped) ((ab a b) (bc b c) (ca c a)))
(= (view twisted) (superpose (True False)))
(= (view path) (superpose (True False)))
(= (view swapped) (superpose (0 1 2)))

; Transport along a walk: translate across each edge in turn.
(= (transport $system $walk $v)
   (foldl-atom $walk $v $value $edge (translate $system $edge $value)))

; A global section: a value at every place, respected by every edge.
(= (value a ($va $vb $vc)) $va)
(= (value b ($va $vb $vc)) $vb)
(= (value c ($va $vb $vc)) $vc)
(= (respects $system $x $edges)
   (if (== $edges ()) True
       (let ($e $i $j) (car-atom $edges)
         (let $rest (cdr-atom $edges)
           (and (== (translate $system $e (value $i $x)) (value $j $x))
                (respects $system $x $rest))))))
(= (assignment $system)
   (let* (($va (view $system)) ($vb (view $system)) ($vc (view $system)))
     ($va $vb $vc)))
(= (section $system)
   (let* (($x (assignment $system))
          ($es (edges $system)))
     (if (respects $system $x $es) $x (empty))))
; Some global section exists.
(= (has-section $system)
   (let $all (collapse (section $system)) (not (== $all ()))))
; The views glue when every value at a extends to a global section
; (forall_invariant_iff_gluing, with sectionsEquivInvariant: sections are the
; values at a that every loop fixes).
(= (extends $system $v)
   (let $hits (collapse (let $x (section $system) (if (== (value a $x) $v) $x (empty))))
     (not (== $hits ()))))
(= (glues $system)
   (let $stuck (collapse (let $v (view $system) (if (extends $system $v) (empty) $v)))
     (== $stuck ())))

; 1. Around the twisted cycle, true comes back false (Twisted.holonomy_moves).
!(transport twisted (ab bc ca) True)
; 2. There is no global section (Twisted.no_sections), and the views do not
;    glue (Twisted.no_gluing).
!(has-section twisted)
!(glues twisted)
; 3. Without the twisted edge the views glue (Twisted.path_glues).
!(has-section path)
!(glues path)
; 4. On the swapped cycle a section exists, the constant 0, and still the
;    views do not glue, since the loop moves 1: the two obstructions differ
;    (Swap.section_exists, Swap.no_gluing, Swap.joint_without_gluing).
!(has-section swapped)
!(glues swapped)

; ---- Part 2: the same facts as checked theorems ----------------------------

!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &thy bool (u 0) (: true bool) (: false bool))
!(pf:equality &thy bool refl@bool subst@bool)
!(set:define &thy truth prop (= truth (all prop (lam p (imp p p)))))
!(set:define &thy falsum prop (= falsum (all prop (lam p p))))
!(set:define &thy negb (-> bool bool) (= (negb true) false) (= (negb false) true))
; constructors are distinct: a prop-valued definition separates them
!(set:define &thy is-true (-> bool prop) (= (is-true true) truth) (= (is-true false) falsum))

; 5. Negation has no fixed point (Bool.not_ne_self), which is why the loop
;    moves every value (Triangle.no_invariant).
!(pf:theorem &thy negb-no-fixed-point (all bool (lam c (imp (eq bool (negb c) c) falsum)))
   (pf:induction bool-ind (lam c (imp (eq bool (negb c) c) falsum))
     (pf:assume h (pf:subst-with bool (lam z (is-true z)) true false
                    (pf:sym bool false true h) (pf:fix p (pf:assume hp hp))))
     (pf:assume h (pf:subst-with bool (lam z (is-true z)) true false
                    h (pf:fix p (pf:assume hp hp))))))
; 6. The three constraints of the twisted cycle are inconsistent
;    (Twisted.no_sections).
!(pf:theorem &thy twisted-no-section
   (all bool (lam a (all bool (lam b (all bool (lam c
     (imp (eq bool a b) (imp (eq bool b c) (imp (eq bool (negb c) a) falsum)))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:assume hab (pf:assume hbc (pf:assume hca
     (pf:imp-elim (pf:all-elim (pf:known negb-no-fixed-point) c)
       (pf:subst-with bool (lam z (eq bool (negb c) z)) a c
                      (pf:trans bool a b c hab hbc) hca)))))))))
; 7. The path has a section: true everywhere (Twisted.path_glues).
!(pf:theorem &thy path-section
   (all prop (lam r (imp (all bool (lam a (all bool (lam b (all bool (lam c
                           (imp (eq bool a b) (imp (eq bool b c) r))))))))
                         r)))
   (pf:fix r (pf:assume k
     (pf:imp-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim k true) true) true)
                               (pf:refl bool true))
                  (pf:refl bool true)))))
; 8. So the argument of 6 depends on the twist: without the third
;    constraint it proves nothing (Twisted.path_glues: the path's constraints
;    have a model).
!(pf:theorem &thy path-no-section
   (all bool (lam a (all bool (lam b (all bool (lam c
     (imp (eq bool a b) (imp (eq bool b c) falsum))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:assume hab (pf:assume hbc
     (pf:imp-elim (pf:all-elim (pf:known negb-no-fixed-point) c)
       (pf:trans bool a b c hab hbc))))))))
