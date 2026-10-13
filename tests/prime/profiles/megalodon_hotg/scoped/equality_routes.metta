(set:profile megalodon-hotg)

; Equality that proofs can use: three routes, side by side.
;
; The set signature's eq has an introduction, the law of extensionality, and
; no elimination: no core rule rewrites with an equation.  Equality that
; substitutes belongs to higher-order logic itself (Church; Andrews' rule R).
; Each route below gives the identity of sets such a rule in a theory of its
; own, and proves the same four statements there:
;   rewrite     an equation moves a membership:  a = b -> a in X -> b in X
;   symmetric   a = b -> b = a
;   transitive  a = b -> b = c -> a = c
;   identical   sets with the same members are identical:
;               X sub Y -> Y sub X -> P X -> P Y, for every predicate P
; For each theorem the fixture prints what its native package assumes (the
; laws and axioms its proof term rests on), the length in symbols of the proof
; as checked (the verbs expanded), and the length of its native proof term.
;
; R1  Substitution is a rule of the logic: (pf:eq-subst P e h) proves P b from
;     e : eq T a b and h : P a, at any carrier T.  A theory adopts the rule by
;     declaring (set:rule eq-subst).  Nothing else is assumed; reflexivity at
;     set is a theorem of extensionality.
; R2  Identity is defined as Leibniz defines it: same x y when every property
;     of x is one of y.  Rewriting, symmetry and transitivity are then logic
;     alone.  That sets with the same members are identical is a law of its
;     own, same-ext, declared as an axiom: extensionality gives only eq.
; R3  Equality is the signature's eq with the two laws pf:equality publishes
;     for a carrier: reflexivity, at set a theorem of extensionality, and
;     substitution, the axiom subst@set, declared again for every carrier.
;
; Without the rule of substitution neither same-ext nor subst@set follows from
; the signature's laws: they hold where every set has a twin with the same
; members that need not belong to the same sets, and eq is having the same
; members; a predicate can tell a set from its twin.  With the rule both are
; theorems, and R1 proves them below.
!(import! &self pf)

; The length of a proof or term in symbols.
(: syntax-length (-> (Data Atom) Number))
(: syntax-length-tail (-> (Data Atom) Number))
(= (syntax-length $t)
   (if (== (get-metatype $t) Expression) (syntax-length-tail $t) 1))
(= (syntax-length-tail $t)
   (if (== $t ()) 0
       (let $h (car-atom $t)
         (let $r (cdr-atom $t)
           (let $nh (syntax-length $h)
             (let $nr (syntax-length-tail $r) (+ $nh $nr)){$nr}){$nh}){$r}){$h}))
; A theorem's row: the laws and axioms its native package rests on, through
; the theorems it cites by name, and the lengths of its proof and of its
; proof term, in which a cited theorem is one constant.
(= (face $route $space $name)
   (let (SetNativeProofV1 $n $prop $proof $term $type $context $rules $rows $digest)
        (set:native-proof $space $name)
     (let $assumed (pf:rests-on $space $name)
       (let $checked (syntax-length $proof)
         (let $native (syntax-length $term)
           ($route $name (assumes $assumed) (proof $checked) (term $native)))))))

; ---- R1: substitution is a rule of the logic
!(bind! &r1 (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(add-atom &r1 (set:rule eq-subst))
!(set:define &r1 Subq (-> set set prop) (= (Subq $X $Y) (all set (lam x (imp (In x $X) (In x $Y))))))
; reflexivity at set, a theorem of extensionality
!(pf:theorem &r1 eq-refl (all set (lam x (eq set x x)))
   (pf:fix x
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known extensionality) x) x)
       (pf:fix z (pf:fix p1 (pf:assume k2
         (pf:imp-elim (pf:imp-elim k2 (pf:assume m m)) (pf:assume m m))))))))
!(pf:theorem &r1 rewrite
   (all set (lam a (all set (lam b (all set (lam X (imp (eq set a b) (imp (In a X) (In b X)))))))))
   (pf:fix a (pf:fix b (pf:fix X (pf:assume e (pf:assume h (pf:eq-subst (lam m (In m X)) e h)))))))
!(pf:theorem &r1 symmetric (all set (lam a (all set (lam b (imp (eq set a b) (eq set b a))))))
   (pf:fix a (pf:fix b (pf:assume e
     (pf:eq-subst (lam m (eq set m a)) e (pf:all-elim (pf:known eq-refl) a))))))
!(pf:theorem &r1 transitive
   (all set (lam a (all set (lam b (all set (lam c (imp (eq set a b) (imp (eq set b c) (eq set a c)))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:assume e1 (pf:assume e2 (pf:eq-subst (lam m (eq set a m)) e2 e1)))))))
!(pf:theorem &r1 identical
   (all set
     (lam X
       (all set
         (lam Y (imp (Subq X Y) (imp (Subq Y X) (all (-> set prop) (lam P (imp (P X) (P Y))))))))))
   (pf:fix X (pf:fix Y (pf:assume xy (pf:assume yx (pf:fix P (pf:assume h
     (pf:eq-subst P
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:known extensionality) X) Y)
         (pf:fix z (pf:fix p1 (pf:assume k2
           (pf:imp-elim (pf:imp-elim k2 (pf:all-elim xy z)) (pf:all-elim yx z))))))
       h))))))))
; at any carrier: a function carrier, and prop
!(pf:theorem &r1 rewrite-function
   (all
     (-> set set)
     (lam F
       (all
         (-> set set)
         (lam G
           (all set
             (lam x (all set (lam X (imp (eq (-> set set) F G) (imp (In (F x) X) (In (G x) X)))))))))))
   (pf:fix F (pf:fix G (pf:fix x (pf:fix X (pf:assume e (pf:assume h
     (pf:eq-subst (lam H (In (H x) X)) e h))))))))
!(pf:theorem &r1 rewrite-prop (all prop (lam p (all prop (lam q (imp (eq prop p q) (imp p q))))))
   (pf:fix p (pf:fix q (pf:assume e (pf:assume h (pf:eq-subst (lam r r) e h))))))
; R1 proves the laws the other routes assume: R2's same-ext is the statement
; identical above, with same unfolded; R3's two laws at set are eq-refl and
; this.
!(pf:theorem &r1 subst
   (all (-> set prop) (lam P (all set (lam a (all set (lam b (imp (eq set a b) (imp (P a) (P b)))))))))
   (pf:fix P (pf:fix a (pf:fix b (pf:assume e (pf:assume h (pf:eq-subst P e h)))))))
; The native package compiles a substitution to the rule constant eqS at the
; equation's instance, declared in the package's context:
;   eqS@A : Pi (P : A -> prop) (a b : A). Holds (eq@A a b) -> Holds (P a) -> Holds (P b)
; in either presentation, and the package links under the identity reading,
; at a carrier of the least universe and at set.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &r1 rewrite)
   (let (PrimeCtxDecl $key $declared $rest) $context ($key $declared)))
!(let $v (try (set:native-proof &r1 symmetric rule-constants)) (car-atom $v))
!(let $p (set:native-proof &r1 rewrite-prop) (let $v (try (set:native-link &r1 identity $p)) (car-atom $v)))
!(let $p (set:native-proof &r1 symmetric) (let $v (try (set:native-link &r1 identity $p)) (car-atom $v)))

; Controls.  The motive is written at the place to rewrite: a motive that
; abstracts the other place does not prove the step.
!(let ($verdict $why)
      (try (set:proves &r1
        (quote (pf:fix a (pf:fix b (pf:fix X (pf:assume e (pf:assume h (pf:eq-subst (lam m (In a m)) e h)))))))
        (quote (all set (lam a (all set (lam b (all set (lam X (imp (eq set a b) (imp (In a X) (In b X))))))))))))
   (let $head (car-atom $why) ($verdict $head)))
; and rewriting one place of a membership of a in itself does not rewrite the
; other
!(let ($verdict $why)
      (try (set:proves &r1
        (quote (pf:fix a (pf:fix b (pf:assume e (pf:assume h (pf:eq-subst (lam m (In m a)) e h))))))
        (quote (all set (lam a (all set (lam b (imp (eq set a b) (imp (In a a) (In a b))))))))))
   (let $head (car-atom $why) ($verdict $head)))
; An equation at a function carrier does not rewrite a set: the motive is read
; at the equation's carrier.
!(let ($verdict $why)
      (try (set:proves &r1
        (quote (pf:fix F (pf:fix G (pf:fix x (pf:fix X (pf:assume e (pf:assume h
          (pf:eq-subst (lam m (In m X)) e h))))))))
        (quote
          (all
            (-> set set)
            (lam F
              (all
                (-> set set)
                (lam G
                  (all set
                    (lam x
                      (all set (lam X (imp (eq (-> set set) F G) (imp (In (F x) X) (In (G x) X))))))))))))))
   (let $head (car-atom $why) ($verdict $head)))
; The major premise must prove an equation.
!(let ($verdict $why)
      (try (set:proves &r1
        (quote (pf:fix a (pf:fix X (pf:assume h (pf:eq-subst (lam m (In m X)) h h)))))
        (quote (all set (lam a (all set (lam X (imp (In a X) (In a X)))))))))
   (let $head (car-atom $why) ($verdict $head)))
; A theory that has not adopted the rule does not check a proof that uses it.
!(bind! &plain (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(try (set:proves &plain
   (quote (pf:fix a (pf:fix b (pf:fix X (pf:assume e (pf:assume h (pf:eq-subst (lam m (In m X)) e h)))))))
   (quote (all set (lam a (all set (lam b (all set (lam X (imp (eq set a b) (imp (In a X) (In b X))))))))))))

; ---- R2: identity defined as Leibniz defines it
!(bind! &r2 (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(set:define &r2 Subq (-> set set prop) (= (Subq $X $Y) (all set (lam x (imp (In x $X) (In x $Y))))))
!(set:define &r2 same (-> set set prop)
   (= (same $x $y) (all (-> set prop) (lam Q (imp (Q $x) (Q $y))))))
; the law of this route: extensionality yields identity, not only an equation.
; It does not make the replacement law usable: a member of a replacement is
; given by an equation, eq set (F x) y, and identifying it needs
; eq set a b -> same a b, substitution at set (R3's subst@set), which same-ext
; does not give: read eq as having the same rank and a replacement as every
; set of the rank of a value, and the laws and same-ext hold while two sets
; of one rank are equal and not the same.
!(set:axiom &r2 same-ext
   (all set (lam X (all set (lam Y (imp (Subq X Y) (imp (Subq Y X) (same X Y))))))))
!(pf:theorem &r2 same-refl (all set (lam x (same x x))) (pf:fix x (pf:fix Q (pf:assume h h))))
!(pf:theorem &r2 rewrite
   (all set (lam a (all set (lam b (all set (lam X (imp (same a b) (imp (In a X) (In b X)))))))))
   (pf:fix a (pf:fix b (pf:fix X (pf:assume e (pf:assume h (pf:imp-elim (pf:all-elim e (lam m (In m X))) h)))))))
!(pf:theorem &r2 symmetric (all set (lam a (all set (lam b (imp (same a b) (same b a))))))
   (pf:fix a (pf:fix b (pf:assume e
     (pf:imp-elim (pf:all-elim e (lam m (same m a))) (pf:all-elim (pf:known same-refl) a))))))
!(pf:theorem &r2 transitive
   (all set (lam a (all set (lam b (all set (lam c (imp (same a b) (imp (same b c) (same a c)))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:assume e1 (pf:assume e2 (pf:imp-elim (pf:all-elim e2 (lam m (same a m))) e1)))))))
!(pf:theorem &r2 identical
   (all set
     (lam X
       (all set
         (lam Y (imp (Subq X Y) (imp (Subq Y X) (all (-> set prop) (lam P (imp (P X) (P Y))))))))))
   (pf:fix X (pf:fix Y (pf:assume xy (pf:assume yx
     (pf:imp-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known same-ext) X) Y) xy) yx))))))

; ---- R3: the signature's eq with the laws of a carrier
!(bind! &r3 (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(set:define &r3 Subq (-> set set prop) (= (Subq $X $Y) (all set (lam x (imp (In x $X) (In x $Y))))))
!(pf:equality &r3 set refl@set subst@set
   (pf:fix x
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known extensionality) x) x)
       (pf:fix z (pf:fix p1 (pf:assume k2
         (pf:imp-elim (pf:imp-elim k2 (pf:assume m m)) (pf:assume m m))))))))
!(pf:theorem &r3 rewrite
   (all set (lam a (all set (lam b (all set (lam X (imp (eq set a b) (imp (In a X) (In b X)))))))))
   (pf:fix a (pf:fix b (pf:fix X (pf:assume e (pf:assume h (pf:subst-with set (lam m (In m X)) a b e h)))))))
!(pf:theorem &r3 symmetric (all set (lam a (all set (lam b (imp (eq set a b) (eq set b a))))))
   (pf:fix a (pf:fix b (pf:assume e (pf:sym set a b e)))))
!(pf:theorem &r3 transitive
   (all set (lam a (all set (lam b (all set (lam c (imp (eq set a b) (imp (eq set b c) (eq set a c)))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:assume e1 (pf:assume e2 (pf:trans set a b c e1 e2)))))))
!(pf:theorem &r3 identical
   (all set
     (lam X
       (all set
         (lam Y (imp (Subq X Y) (imp (Subq Y X) (all (-> set prop) (lam P (imp (P X) (P Y))))))))))
   (pf:fix X (pf:fix Y (pf:assume xy (pf:assume yx (pf:fix P (pf:assume h
     (pf:subst-with set P X Y
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:known extensionality) X) Y)
         (pf:fix z (pf:fix p1 (pf:assume k2
           (pf:imp-elim (pf:imp-elim k2 (pf:all-elim xy z)) (pf:all-elim yx z))))))
       h))))))))
; The laws are per carrier: at a function carrier, where no substitution is
; declared, the verb symmetry expands to a citation of nothing, and the proof
; is not checked.
!(let (quote $proof) (lift (pf:expand (pf:fix F (pf:fix G (pf:assume e (pf:sym (-> set set) F G e))))))
   (let ($verdict $why)
        (try (set:proves &r3 (quote $proof)
          (quote (all (-> set set) (lam F (all (-> set set) (lam G (imp (eq (-> set set) F G) (eq (-> set set) G F)))))))))
     (let $head (car-atom $why) ($verdict $head))))

; ---- side by side: what each route assumes, what each proves, proof lengths
!(match &r1 (set:rule $rule) (adopted r1 $rule))
!(set:known-proposition &r2 same-ext)
!(set:known-proposition &r3 subst@set)
!(face r1 &r1 rewrite)
!(face r2 &r2 rewrite)
!(face r3 &r3 rewrite)
!(face r1 &r1 symmetric)
!(face r2 &r2 symmetric)
!(face r3 &r3 symmetric)
!(face r1 &r1 transitive)
!(face r2 &r2 transitive)
!(face r3 &r3 transitive)
!(face r1 &r1 identical)
!(face r2 &r2 identical)
!(face r3 &r3 identical)
!(face r1 &r1 rewrite-function)
!(face r1 &r1 rewrite-prop)
!(face r1 &r1 subst)
; The rule of substitution is derived in the extensional calculus of
; higher-order logic: eq x y -> forall P. P x -> P y, as a closed proof at
; every type, built from eqAppArg and eqPropEL.
!(quote (rule eq-subst
  "Mettapedia.TypeTheory.Calculi.CumulativePiSigmaId.HOLLeibnizProofComparison.Source.primitiveToLeibniz"))
