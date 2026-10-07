(set:profile megalodon-hotg)

; The Naproche-ZF curriculum with the signature's equation as the identity of
; sets.  The theorems, definitions and proofs are those of the Naproche
; curriculum beside this one, in its order, after the standard library of the
; Naproche-ZF prover (sets, relations, functions, orders and ordinals) and its
; two showcase theorems, Cantor's theorem and the Burali-Forti antinomy.  The
; one difference is equality.  There the identity of sets is a definition,
; Leibniz's same; here the theory adopts the rule of
; substitution, (set:rule eq-subst), and the identity of sets is eq set, the
; equation that extensionality concludes.  A step that used an identity,
;   (pf:imp-elim (pf:all-elim h (lam m B)) k)     with h : same x y,
; is now
;   (pf:eq-subst (lam m B) h k)                   with h : eq set x y,
; and the bridge from the identity to the equation (same-to-eq) is gone.  The
; statements that curriculum refuses at its end, which use an equation as an
; identity, are proved here.  Every proof uses the rules of the logic, the
; citation of a law or an earlier theorem, and substitution, so each theorem
; has a native package, its proof term, and can be written as a Megalodon
; article and checked again; there an equation is Megalodon's own equality,
; which is Leibniz's.
;
; Reflexivity is not a rule: at set it is a theorem of extensionality
; (eq-refl), so a proof that needs it rests on extensionality, where the
; Leibniz identity's reflexivity rests on nothing.
;
; Pairs.  The signature has no pairing law, and replacement takes a function,
; so the unordered pair is defined, as Megalodon defines it, by replacement
; over the two-element set Power (Power Empty) with a function that chooses its
; value by Eps_set.  That needs the law of the choice operator, EpsI, which the
; signature does not state; it is the one axiom here, the statement of
; Megalodon's Eps_i_ax and the same axiom as in the Megalodon curriculum.
!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(add-atom &thy (set:rule eq-subst))

; ---- logic: the connectives as definitions (Naproche-ZF reads its
; statements in higher-order logic; existence here ranges over sets)
!(set:define &thy not (-> prop prop) (= (not $A) (imp $A Falsum)))
!(set:define &thy and (-> prop prop prop)
   (= (and $A $B) (all prop (lam p (imp (imp $A (imp $B p)) p)))))
!(set:define &thy or (-> prop prop prop)
   (= (or $A $B) (all prop (lam p (imp (imp $A p) (imp (imp $B p) p))))))
!(set:define &thy iff (-> prop prop prop) (= (iff $A $B) (and (imp $A $B) (imp $B $A))))
!(set:define &thy ex (-> (-> set prop) prop)
   (= (ex $P) (all prop (lam p (imp (all set (lam x (imp ($P x) p))) p)))))

; ---- sets (Naproche-ZF library: sets): inclusion, the laws taken apart
; into introduction and elimination, extensionality, and equality
!(set:define &thy Subq (-> set set prop) (= (Subq $X $Y) (all set (lam x (imp (In x $X) (In x $Y))))))
; sets: every set includes itself
!(pf:theorem &thy subq-refl (all set (lam X (Subq X X)))
   (pf:fix X (pf:fix x (pf:assume h h))))
; sets: inclusion is transitive
!(pf:theorem &thy subq-trans
   (all set (lam X (all set (lam Y (all set (lam Z (imp (Subq X Y) (imp (Subq Y Z) (Subq X Z)))))))))
   (pf:fix X (pf:fix Y (pf:fix Z (pf:assume xy (pf:assume yz (pf:fix x (pf:assume h
     (pf:imp-elim (pf:all-elim yz x) (pf:imp-elim (pf:all-elim xy x) h))))))))))
; sets: a set is a member of the power set of each set that includes it
!(pf:theorem &thy power-intro (all set (lam a (all set (lam b (imp (Subq b a) (In b (Power a)))))))
   (pf:fix a (pf:fix b (pf:assume h
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known powerLaw) a) b) (In b (Power a)))
       (pf:assume f1 (pf:assume g2 (pf:imp-elim g2 h))))))))
; sets: a member of a power set is included in the set
!(pf:theorem &thy power-elim (all set (lam a (all set (lam b (imp (In b (Power a)) (Subq b a))))))
   (pf:fix a (pf:fix b (pf:assume h
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known powerLaw) a) b) (Subq b a))
       (pf:assume f1 (pf:assume g2 (pf:imp-elim f1 h))))))))
; sets: a set is a member of its own power set
!(pf:theorem &thy power-self (all set (lam X (In X (Power X))))
   (pf:fix X
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known power-intro) X) X)
       (pf:all-elim (pf:known subq-refl) X))))
; sets: the empty set is included in every set
!(pf:theorem &thy empty-subq (all set (lam X (Subq Empty X)))
   (pf:fix X (pf:fix x (pf:assume h
     (pf:all-elim (pf:imp-elim (pf:all-elim (pf:known emptyLaw) x) h) (In x X))))))
; sets: a member of a with the property P is a member of Sep a P
!(pf:theorem &thy sep-intro
   (all set
     (lam a (all (-> set prop) (lam P (all set (lam x (imp (In x a) (imp (P x) (In x (Sep a P))))))))))
   (pf:fix a (pf:fix P (pf:fix x (pf:assume hx (pf:assume hp
     (pf:imp-elim
       (pf:all-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known separationLaw) a) P) x)
         (In x (Sep a P)))
       (pf:assume f3 (pf:assume g4
         (pf:imp-elim g4 (pf:fix p1 (pf:assume k2 (pf:imp-elim (pf:imp-elim k2 hx) hp)))))))))))))
; sets: a member of Sep a P is a member of a
!(pf:theorem &thy sep-elim-in
   (all set (lam a (all (-> set prop) (lam P (all set (lam x (imp (In x (Sep a P)) (In x a))))))))
   (pf:fix a (pf:fix P (pf:fix x (pf:assume h
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim
           (pf:all-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known separationLaw) a) P) x)
             (and (In x a) (P x)))
           (pf:assume f1 (pf:assume g2 (pf:imp-elim f1 h))))
         (In x a))
       (pf:assume l3 (pf:assume r4 l3))))))))
; sets: and it has the property P
!(pf:theorem &thy sep-elim-prop
   (all set (lam a (all (-> set prop) (lam P (all set (lam x (imp (In x (Sep a P)) (P x))))))))
   (pf:fix a (pf:fix P (pf:fix x (pf:assume h
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim
           (pf:all-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known separationLaw) a) P) x)
             (and (In x a) (P x)))
           (pf:assume f1 (pf:assume g2 (pf:imp-elim f1 h))))
         (P x))
       (pf:assume l3 (pf:assume r4 r4))))))))
; sets: Sep a P is included in a
!(pf:theorem &thy sep-subq (all set (lam a (all (-> set prop) (lam P (Subq (Sep a P) a)))))
   (pf:fix a (pf:fix P (pf:fix x (pf:assume h
     (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known sep-elim-in) a) P) x) h))))))
; sets: a member of a member of a is a member of Union a
!(pf:theorem &thy union-intro
   (all set (lam a (all set (lam x (all set (lam y (imp (In x y) (imp (In y a) (In x (Union a))))))))))
   (pf:fix a (pf:fix x (pf:fix y (pf:assume hxy (pf:assume hya
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known unionLaw) a) x) (In x (Union a)))
       (pf:assume f5 (pf:assume g6
         (pf:imp-elim g6
           (pf:fix p3 (pf:assume k4
             (pf:imp-elim
               (pf:all-elim k4 y)
               (pf:fix p1 (pf:assume k2 (pf:imp-elim (pf:imp-elim k2 hya) hxy))))))))))))))))
; sets: a member of Union a is a member of some member of a
!(pf:theorem &thy union-elim
   (all set
     (lam a
       (all set
         (lam x
           (imp
             (In x (Union a))
             (all prop (lam p (imp (all set (lam y (imp (In y a) (imp (In x y) p)))) p))))))))
   (pf:fix a (pf:fix x (pf:assume h (pf:fix p (pf:assume k
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim
           (pf:all-elim
             (pf:all-elim (pf:all-elim (pf:known unionLaw) a) x)
             (ex (lam y (and (In y a) (In x y)))))
           (pf:assume f1 (pf:assume g2 (pf:imp-elim f1 h))))
         p)
       (pf:fix y (pf:assume hy
         (pf:imp-elim
           (pf:all-elim hy p)
           (pf:assume hya (pf:assume hxy (pf:imp-elim (pf:imp-elim (pf:all-elim k y) hya) hxy)))))))))))))
; sets: F x is a member of Repl a F when x is a member of a
!(pf:theorem &thy repl-intro
   (all set
     (lam a
       (all
         (-> set set)
         (lam F
           (all set (lam x (all set (lam y (imp (In x a) (imp (eq set (F x) y) (In y (Repl a F))))))))))))
   (pf:fix a (pf:fix F (pf:fix x (pf:fix y (pf:assume hx (pf:assume he
     (pf:imp-elim
       (pf:all-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known replacementLaw) a) F) y)
         (In y (Repl a F)))
       (pf:assume f5 (pf:assume g6
         (pf:imp-elim g6
           (pf:fix p3 (pf:assume k4
             (pf:imp-elim
               (pf:all-elim k4 x)
               (pf:fix p1 (pf:assume k2 (pf:imp-elim (pf:imp-elim k2 hx) he)))))))))))))))))
; sets: the equation of a set with itself, from extensionality
!(pf:theorem &thy eq-refl (all set (lam x (eq set x x)))
   (pf:fix x
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known extensionality) x) x)
       (pf:fix z (pf:fix p1 (pf:assume k2
         (pf:imp-elim (pf:imp-elim k2 (pf:assume m m)) (pf:assume m m))))))))
; sets: sets that include each other are equal (Naproche-ZF: "by set extensionality")
!(pf:theorem &thy set-ext
   (all set (lam X (all set (lam Y (imp (Subq X Y) (imp (Subq Y X) (eq set X Y)))))))
   (pf:fix X (pf:fix Y (pf:assume xy (pf:assume yx
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known extensionality) X) Y)
       (pf:fix z (pf:fix p1 (pf:assume k2
         (pf:imp-elim (pf:imp-elim k2 (pf:all-elim xy z)) (pf:all-elim yx z)))))))))))
; sets: equality is symmetric, by substitution in eq set z x
!(pf:theorem &thy eq-sym (all set (lam x (all set (lam y (imp (eq set x y) (eq set y x))))))
   (pf:fix x (pf:fix y (pf:assume h
     (pf:eq-subst (lam z (eq set z x)) h (pf:all-elim (pf:known eq-refl) x))))))
; sets: equality is transitive, by substitution in eq set x w
!(pf:theorem &thy eq-trans
   (all set (lam x (all set (lam y (all set (lam z (imp (eq set x y) (imp (eq set y z) (eq set x z)))))))))
   (pf:fix x (pf:fix y (pf:fix z (pf:assume hxy (pf:assume hyz
     (pf:eq-subst (lam w (eq set x w)) hyz hxy)))))))

; ---- Russell's antinomy, foundation, and no set of all sets
; sets (Russell): a proposition equivalent to its own negation is absurd
!(pf:theorem &thy iff-not-absurd (all prop (lam A (not (iff A (not A)))))
   (pf:fix A (pf:assume h
     (pf:imp-elim
       (pf:all-elim h Falsum)
       (pf:assume fwd (pf:assume bwd
         (pf:imp-elim
           (pf:imp-elim fwd (pf:imp-elim bwd (pf:assume a (pf:imp-elim (pf:imp-elim fwd a) a))))
           (pf:imp-elim bwd (pf:assume a (pf:imp-elim (pf:imp-elim fwd a) a))))))))))
; sets (Russell): no set has as members exactly the sets that are not members of themselves
!(pf:theorem &thy russell (not (ex (lam R (all set (lam x (iff (In x R) (not (In x x))))))))
   (pf:assume h
     (pf:imp-elim
       (pf:all-elim h Falsum)
       (pf:fix R (pf:assume hR
         (pf:imp-elim (pf:all-elim (pf:known iff-not-absurd) (In R R)) (pf:all-elim hR R)))))))
; sets (foundation): no set is a member of itself, by induction on membership
!(pf:theorem &thy in-irrefl (all set (lam x (not (In x x))))
   (pf:imp-elim
     (pf:all-elim (pf:known setInduction) (lam x (not (In x x))))
     (pf:fix X (pf:assume ih (pf:assume hXX (pf:imp-elim (pf:imp-elim (pf:all-elim ih X) hXX) hXX))))))
; sets (foundation): no two sets are members of each other
!(pf:theorem &thy in-asym (all set (lam x (all set (lam y (imp (In x y) (not (In y x)))))))
   (pf:imp-elim
     (pf:all-elim (pf:known setInduction) (lam x (all set (lam y (imp (In x y) (not (In y x)))))))
     (pf:fix X (pf:assume ih (pf:fix y (pf:assume hXy (pf:assume hyX
       (pf:imp-elim (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim ih y) hyX) X) hyX) hXy))))))))
; sets: there is no set of all sets, else separation would give the Russell set
!(pf:theorem &thy no-universal-set (not (ex (lam V (all set (lam x (In x V))))))
   (pf:assume h
     (pf:imp-elim
       (pf:all-elim h Falsum)
       (pf:fix V (pf:assume hV
         (pf:imp-elim
           (pf:known russell)
           (pf:fix p3 (pf:assume k4
             (pf:imp-elim
               (pf:all-elim k4 (Sep V (lam x (not (In x x)))))
               (pf:fix x (pf:fix p1 (pf:assume k2
                 (pf:imp-elim
                   (pf:imp-elim k2
                     (pf:assume hin
                       (pf:imp-elim
                         (pf:all-elim
                           (pf:all-elim (pf:all-elim (pf:known sep-elim-prop) V) (lam x (not (In x x))))
                           x)
                         hin)))
                   (pf:assume hn
                     (pf:imp-elim
                       (pf:imp-elim
                         (pf:all-elim
                           (pf:all-elim (pf:all-elim (pf:known sep-intro) V) (lam x (not (In x x))))
                           x)
                         (pf:all-elim hV x))
                       hn)))))))))))))))

; ---- pairs (Naproche-ZF library: relations): the unordered pair, the
; singleton, and Kuratowski's ordered pair {{a}, {a, b}}
; the law of the choice operator: Eps_set P has the property P when some set has it
!(set:axiom &thy EpsI (all (-> set prop) (lam P (all set (lam x (imp (P x) (P (Eps_set P))))))))
; pick a b X is a when Empty is a member of X, and b when it is not
!(set:define &thy pick (-> set set set set)
   (= (pick $a $b $X)
     (Eps_set (lam w (and (imp (In Empty $X) (eq set w $a)) (imp (not (In Empty $X)) (eq set w $b)))))))
; both a b has a and b among its members: replacement over {Empty, {Empty}}
!(set:define &thy both (-> set set set)
   (= (both $a $b) (Repl (Power (Power Empty)) (lam X (pick $a $b X)))))
; the unordered pair: the members of both a b that are a or b
!(set:define &thy UPair (-> set set set)
   (= (UPair $a $b) (Sep (both $a $b) (lam w (or (eq set w $a) (eq set w $b))))))
; the singleton, and Kuratowski's ordered pair
!(set:define &thy Sing (-> set set) (= (Sing $a) (Sep (Power $a) (lam w (eq set w $a)))))
!(set:define &thy kpair (-> set set set) (= (kpair $a $b) (UPair (Sing $a) (UPair $a $b))))
; relations (pairs): the chosen set is a when Empty is a member of X
!(pf:theorem &thy pick-yes
   (all set (lam a (all set (lam b (all set (lam X (imp (In Empty X) (eq set (pick a b X) a))))))))
   (pf:fix a (pf:fix b (pf:fix X (pf:assume hX
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:imp-elim
             (pf:all-elim
               (pf:all-elim
                 (pf:known EpsI)
                 (lam w (and (imp (In Empty X) (eq set w a)) (imp (not (In Empty X)) (eq set w b)))))
               a)
             (pf:fix p1 (pf:assume k2
               (pf:imp-elim
                 (pf:imp-elim k2 (pf:assume h0 (pf:all-elim (pf:known eq-refl) a)))
                 (pf:assume n (pf:all-elim (pf:imp-elim n hX) (eq set a b)))))))
           (imp (In Empty X) (eq set (pick a b X) a)))
         (pf:assume l3 (pf:assume r4 l3)))
       hX))))))
; relations (pairs): and b when it is not
!(pf:theorem &thy pick-no
   (all set (lam a (all set (lam b (all set (lam X (imp (not (In Empty X)) (eq set (pick a b X) b))))))))
   (pf:fix a (pf:fix b (pf:fix X (pf:assume hX
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:imp-elim
             (pf:all-elim
               (pf:all-elim
                 (pf:known EpsI)
                 (lam w (and (imp (In Empty X) (eq set w a)) (imp (not (In Empty X)) (eq set w b)))))
               b)
             (pf:fix p1 (pf:assume k2
               (pf:imp-elim
                 (pf:imp-elim k2 (pf:assume h0 (pf:all-elim (pf:imp-elim hX h0) (eq set b a))))
                 (pf:assume n (pf:all-elim (pf:known eq-refl) b))))))
           (imp (not (In Empty X)) (eq set (pick a b X) b)))
         (pf:assume l3 (pf:assume r4 r4)))
       hX))))))
; relations (pairs): a is a member of both a b, the replacement over {Empty, {Empty}}
!(pf:theorem &thy both-left (all set (lam a (all set (lam b (In a (both a b))))))
   (pf:fix a (pf:fix b
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:all-elim
             (pf:all-elim
               (pf:all-elim (pf:known repl-intro) (Power (Power Empty)))
               (lam X (pick a b X)))
             (Power Empty))
           a)
         (pf:all-elim (pf:known power-self) (Power Empty)))
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known pick-yes) a) b) (Power Empty))
         (pf:all-elim (pf:known power-self) Empty))))))
; relations (pairs): and so is b
!(pf:theorem &thy both-right (all set (lam a (all set (lam b (In b (both a b))))))
   (pf:fix a (pf:fix b
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:all-elim
             (pf:all-elim
               (pf:all-elim (pf:known repl-intro) (Power (Power Empty)))
               (lam X (pick a b X)))
             Empty)
           b)
         (pf:imp-elim
           (pf:all-elim (pf:all-elim (pf:known power-intro) (Power Empty)) Empty)
           (pf:all-elim (pf:known empty-subq) (Power Empty))))
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known pick-no) a) b) Empty)
         (pf:all-elim (pf:known emptyLaw) Empty))))))
; relations (pairs): a is a member of the unordered pair of a and b
!(pf:theorem &thy upair-left (all set (lam a (all set (lam b (In a (UPair a b))))))
   (pf:fix a (pf:fix b
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:all-elim
             (pf:all-elim (pf:known sep-intro) (both a b))
             (lam w (or (eq set w a) (eq set w b))))
           a)
         (pf:all-elim (pf:all-elim (pf:known both-left) a) b))
       (pf:fix p1 (pf:assume l2 (pf:assume r3 (pf:imp-elim l2 (pf:all-elim (pf:known eq-refl) a)))))))))
; relations (pairs): and so is b
!(pf:theorem &thy upair-right (all set (lam a (all set (lam b (In b (UPair a b))))))
   (pf:fix a (pf:fix b
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:all-elim
             (pf:all-elim (pf:known sep-intro) (both a b))
             (lam w (or (eq set w a) (eq set w b))))
           b)
         (pf:all-elim (pf:all-elim (pf:known both-right) a) b))
       (pf:fix p1 (pf:assume l2 (pf:assume r3 (pf:imp-elim r3 (pf:all-elim (pf:known eq-refl) b)))))))))
; relations (pairs): and nothing else is
!(pf:theorem &thy upair-elim
   (all set
     (lam a (all set (lam b (all set (lam z (imp (In z (UPair a b)) (or (eq set z a) (eq set z b)))))))))
   (pf:fix a (pf:fix b (pf:fix z (pf:assume h
     (pf:imp-elim
       (pf:all-elim
         (pf:all-elim
           (pf:all-elim (pf:known sep-elim-prop) (both a b))
           (lam w (or (eq set w a) (eq set w b))))
         z)
       h))))))
; relations (pairs): a is a member of its singleton, separated from its power set without choice
!(pf:theorem &thy sing-intro (all set (lam a (In a (Sing a))))
   (pf:fix a
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known sep-intro) (Power a)) (lam w (eq set w a))) a)
         (pf:all-elim (pf:known power-self) a))
       (pf:all-elim (pf:known eq-refl) a))))
; relations (pairs): and nothing else is
!(pf:theorem &thy sing-elim (all set (lam a (all set (lam z (imp (In z (Sing a)) (eq set z a))))))
   (pf:fix a (pf:fix z (pf:assume h
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known sep-elim-prop) (Power a)) (lam w (eq set w a))) z)
       h)))))
; relations (pairs): sets with the same singleton are the same
!(pf:theorem &thy sing-inj (all set (lam a (all set (lam c (imp (eq set (Sing a) (Sing c)) (eq set a c))))))
   (pf:fix a (pf:fix c (pf:assume h
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known sing-elim) c) a)
       (pf:eq-subst (lam m (In a m)) h (pf:all-elim (pf:known sing-intro) a)))))))
; relations (pairs): a pair that is a singleton has equal members
!(pf:theorem &thy upair-sing
   (all set
     (lam a
       (all set (lam b (all set (lam c (imp (eq set (UPair a b) (Sing c)) (and (eq set a c) (eq set b c)))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:assume h (pf:fix p1 (pf:assume k2
     (pf:imp-elim
       (pf:imp-elim k2
         (pf:imp-elim
           (pf:all-elim (pf:all-elim (pf:known sing-elim) c) a)
           (pf:eq-subst (lam m (In a m)) h (pf:all-elim (pf:all-elim (pf:known upair-left) a) b))))
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:known sing-elim) c) b)
         (pf:eq-subst (lam m (In b m)) h (pf:all-elim (pf:all-elim (pf:known upair-right) a) b)))))))))))
; relations (pairs): equal pairs with equal first members have equal second members
!(pf:theorem &thy upair-second
   (all set
     (lam a
       (all set
         (lam b
           (all set
             (lam c (all set (lam d (imp (eq set a c) (imp (eq set (UPair a b) (UPair c d)) (eq set b d)))))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:fix d (pf:assume hac (pf:assume h
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-elim) c) d) b)
             (pf:eq-subst (lam m (In b m)) h (pf:all-elim (pf:all-elim (pf:known upair-right) a) b)))
           (eq set b d))
         (pf:assume hbc
           (pf:imp-elim
             (pf:imp-elim
               (pf:all-elim
                 (pf:imp-elim
                   (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-elim) a) b) d)
                   (pf:eq-subst
                     (lam m (In d m))
                     (pf:imp-elim
                       (pf:all-elim (pf:all-elim (pf:known eq-sym) (UPair a b)) (UPair c d))
                       h)
                     (pf:all-elim (pf:all-elim (pf:known upair-right) c) d)))
                 (eq set b d))
               (pf:assume hda
                 (pf:imp-elim
                   (pf:imp-elim
                     (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans) b) c) d)
                     hbc)
                   (pf:imp-elim
                     (pf:imp-elim
                       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans) c) a) d)
                       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym) a) c) hac))
                     (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym) d) a) hda)))))
             (pf:assume hdb (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym) d) b) hdb)))))
       (pf:assume hbd hbd)))))))))
; relations (pairs): Kuratowski's characteristic lemma, first members
!(pf:theorem &thy kpair-inj-first
   (all set
     (lam a
       (all set
         (lam b (all set (lam c (all set (lam d (imp (eq set (kpair a b) (kpair c d)) (eq set a c))))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:fix d (pf:assume H
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:imp-elim
             (pf:all-elim
               (pf:all-elim (pf:all-elim (pf:known upair-elim) (Sing c)) (UPair c d))
               (Sing a))
             (pf:eq-subst
               (lam m (In (Sing a) m))
               H
               (pf:all-elim (pf:all-elim (pf:known upair-left) (Sing a)) (UPair a b))))
           (eq set a c))
         (pf:assume hA (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known sing-inj) a) c) hA)))
       (pf:assume hB
         (pf:imp-elim
           (pf:all-elim (pf:all-elim (pf:known eq-sym) c) a)
           (pf:imp-elim
             (pf:all-elim
               (pf:imp-elim
                 (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-sing) c) d) a)
                 (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym) (Sing a)) (UPair c d)) hB))
               (eq set c a))
             (pf:assume l1 (pf:assume r2 l1))))))))))))
; relations (pairs): Kuratowski's characteristic lemma, second members
!(pf:theorem &thy kpair-inj-second
   (all set
     (lam a
       (all set
         (lam b (all set (lam c (all set (lam d (imp (eq set (kpair a b) (kpair c d)) (eq set b d))))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:fix d (pf:assume H
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:imp-elim
             (pf:all-elim
               (pf:all-elim (pf:all-elim (pf:known upair-elim) (Sing c)) (UPair c d))
               (UPair a b))
             (pf:eq-subst
               (lam m (In (UPair a b) m))
               H
               (pf:all-elim (pf:all-elim (pf:known upair-right) (Sing a)) (UPair a b))))
           (eq set b d))
         (pf:assume hA
           (pf:imp-elim
             (pf:imp-elim
               (pf:all-elim
                 (pf:imp-elim
                   (pf:all-elim
                     (pf:all-elim (pf:all-elim (pf:known upair-elim) (Sing a)) (UPair a b))
                     (UPair c d))
                   (pf:eq-subst
                     (lam m (In (UPair c d) m))
                     (pf:imp-elim
                       (pf:all-elim (pf:all-elim (pf:known eq-sym) (kpair a b)) (kpair c d))
                       H)
                     (pf:all-elim (pf:all-elim (pf:known upair-right) (Sing c)) (UPair c d))))
                 (eq set b d))
               (pf:assume hA1
                 (pf:imp-elim
                   (pf:imp-elim
                     (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans) b) c) d)
                     (pf:imp-elim
                       (pf:all-elim
                         (pf:imp-elim
                           (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-sing) a) b) c)
                           hA)
                         (eq set b c))
                       (pf:assume l1 (pf:assume r2 r2))))
                   (pf:imp-elim
                     (pf:imp-elim
                       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans) c) a) d)
                       (pf:imp-elim
                         (pf:all-elim
                           (pf:imp-elim
                             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-sing) c) d) a)
                             hA1)
                           (eq set c a))
                         (pf:assume l3 (pf:assume r4 l3))))
                     (pf:imp-elim
                       (pf:all-elim (pf:all-elim (pf:known eq-sym) d) a)
                       (pf:imp-elim
                         (pf:all-elim
                           (pf:imp-elim
                             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-sing) c) d) a)
                             hA1)
                           (eq set d a))
                         (pf:assume l5 (pf:assume r6 r6))))))))
             (pf:assume hA2
               (pf:imp-elim
                 (pf:all-elim (pf:all-elim (pf:known eq-sym) d) b)
                 (pf:imp-elim
                   (pf:imp-elim
                     (pf:all-elim
                       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-second) c) d) a)
                       b)
                     (pf:imp-elim
                       (pf:all-elim (pf:all-elim (pf:known eq-sym) a) c)
                       (pf:imp-elim
                         (pf:all-elim
                           (pf:all-elim (pf:all-elim (pf:all-elim (pf:known kpair-inj-first) a) b) c)
                           d)
                         H)))
                   hA2))))))
       (pf:assume hB
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-second) a) b) c) d)
             (pf:imp-elim
               (pf:all-elim
                 (pf:all-elim (pf:all-elim (pf:all-elim (pf:known kpair-inj-first) a) b) c)
                 d)
               H))
           hB)))))))))
; relations (pairs): Kuratowski: ordered pairs are the same exactly when their members are, in order
!(pf:theorem &thy kpair-inj
   (all set
     (lam a
       (all set
         (lam b
           (all set
             (lam c (all set (lam d (imp (eq set (kpair a b) (kpair c d)) (and (eq set a c) (eq set b d)))))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:fix d (pf:assume H (pf:fix p1 (pf:assume k2
     (pf:imp-elim
       (pf:imp-elim k2
         (pf:imp-elim
           (pf:all-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known kpair-inj-first) a) b) c) d)
           H))
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known kpair-inj-second) a) b) c) d)
         H))))))))))
; relations (pairs): and pairs of the same members, in order, are the same
!(pf:theorem &thy kpair-cong
   (all set
     (lam a
       (all set
         (lam b
           (all set
             (lam c (all set (lam d (imp (eq set a c) (imp (eq set b d) (eq set (kpair a b) (kpair c d))))))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:fix d (pf:assume hac (pf:assume hbd
     (pf:eq-subst
       (lam m (eq set (kpair a b) (kpair c m)))
       hbd
       (pf:eq-subst
         (lam m (eq set (kpair a b) (kpair m b)))
         hac
         (pf:all-elim (pf:known eq-refl) (kpair a b)))))))))))

; ---- relations (Naproche-ZF library: relations): the union of two sets, a
; relation as a set of ordered pairs, its domain and range, and the cartesian
; product separated from the power set of the power set of A and B together
!(set:define &thy binunion (-> set set set) (= (binunion $X $Y) (Union (UPair $X $Y))))
!(set:define &thy relation (-> set prop)
   (= (relation $R) (all set (lam z (imp (In z $R) (ex (lam x (ex (lam y (eq set z (kpair x y)))))))))))
!(set:define &thy dom (-> set set)
   (= (dom $R) (Sep (Union (Union $R)) (lam x (ex (lam y (In (kpair x y) $R)))))))
!(set:define &thy ran (-> set set)
   (= (ran $R) (Sep (Union (Union $R)) (lam y (ex (lam x (In (kpair x y) $R)))))))
!(set:define &thy prod (-> set set set)
   (= (prod $A $B)
     (Sep
       (Power (Power (binunion $A $B)))
       (lam z (ex (lam x (and (In x $A) (ex (lam y (and (In y $B) (eq set z (kpair x y))))))))))))
; sets: a member of X is a member of the union of X and Y
!(pf:theorem &thy binunion-left
   (all set (lam X (all set (lam Y (all set (lam x (imp (In x X) (In x (binunion X Y)))))))))
   (pf:fix X (pf:fix Y (pf:fix x (pf:assume h
     (pf:imp-elim
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known union-intro) (UPair X Y)) x) X) h)
       (pf:all-elim (pf:all-elim (pf:known upair-left) X) Y)))))))
; sets: and so is a member of Y
!(pf:theorem &thy binunion-right
   (all set (lam X (all set (lam Y (all set (lam x (imp (In x Y) (In x (binunion X Y)))))))))
   (pf:fix X (pf:fix Y (pf:fix x (pf:assume h
     (pf:imp-elim
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known union-intro) (UPair X Y)) x) Y) h)
       (pf:all-elim (pf:all-elim (pf:known upair-right) X) Y)))))))
; sets: and nothing else is
!(pf:theorem &thy binunion-elim
   (all set
     (lam X (all set (lam Y (all set (lam x (imp (In x (binunion X Y)) (or (In x X) (In x Y)))))))))
   (pf:fix X (pf:fix Y (pf:fix x (pf:assume h
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known union-elim) (UPair X Y)) x) h)
         (or (In x X) (In x Y)))
       (pf:fix y (pf:assume hy (pf:assume hxy
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim
               (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-elim) X) Y) y) hy)
               (or (In x X) (In x Y)))
             (pf:assume hyX (pf:fix p1 (pf:assume l2 (pf:assume r3
               (pf:imp-elim l2 (pf:eq-subst (lam m (In x m)) hyX hxy)))))))
           (pf:assume hyY (pf:fix p4 (pf:assume l5 (pf:assume r6
             (pf:imp-elim r6 (pf:eq-subst (lam m (In x m)) hyY hxy))))))))))))))))
; relations: the first member of a pair in R is in the domain of R
!(pf:theorem &thy dom-intro
   (all set (lam R (all set (lam x (all set (lam y (imp (In (kpair x y) R) (In x (dom R)))))))))
   (pf:fix R (pf:fix x (pf:fix y (pf:assume h
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:all-elim
             (pf:all-elim (pf:known sep-intro) (Union (Union R)))
             (lam x (ex (lam y (In (kpair x y) R)))))
           x)
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known union-intro) (Union R)) x) (Sing x))
             (pf:all-elim (pf:known sing-intro) x))
           (pf:imp-elim
             (pf:imp-elim
               (pf:all-elim (pf:all-elim (pf:all-elim (pf:known union-intro) R) (Sing x)) (kpair x y))
               (pf:all-elim (pf:all-elim (pf:known upair-left) (Sing x)) (UPair x y)))
             h)))
       (pf:fix p1 (pf:assume k2 (pf:imp-elim (pf:all-elim k2 y) h)))))))))
; relations: a member of the domain of R is the first member of a pair in R
!(pf:theorem &thy dom-elim
   (all set (lam R (all set (lam x (imp (In x (dom R)) (ex (lam y (In (kpair x y) R))))))))
   (pf:fix R (pf:fix x (pf:assume h
     (pf:imp-elim
       (pf:all-elim
         (pf:all-elim
           (pf:all-elim (pf:known sep-elim-prop) (Union (Union R)))
           (lam x (ex (lam y (In (kpair x y) R)))))
         x)
       h)))))
; relations: the second member of a pair in R is in the range of R
!(pf:theorem &thy ran-intro
   (all set (lam R (all set (lam x (all set (lam y (imp (In (kpair x y) R) (In y (ran R)))))))))
   (pf:fix R (pf:fix x (pf:fix y (pf:assume h
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:all-elim
             (pf:all-elim (pf:known sep-intro) (Union (Union R)))
             (lam y (ex (lam x (In (kpair x y) R)))))
           y)
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known union-intro) (Union R)) y) (UPair x y))
             (pf:all-elim (pf:all-elim (pf:known upair-right) x) y))
           (pf:imp-elim
             (pf:imp-elim
               (pf:all-elim
                 (pf:all-elim (pf:all-elim (pf:known union-intro) R) (UPair x y))
                 (kpair x y))
               (pf:all-elim (pf:all-elim (pf:known upair-right) (Sing x)) (UPair x y)))
             h)))
       (pf:fix p1 (pf:assume k2 (pf:imp-elim (pf:all-elim k2 x) h)))))))))
; relations: a member of the range of R is the second member of a pair in R
!(pf:theorem &thy ran-elim
   (all set (lam R (all set (lam y (imp (In y (ran R)) (ex (lam x (In (kpair x y) R))))))))
   (pf:fix R (pf:fix y (pf:assume h
     (pf:imp-elim
       (pf:all-elim
         (pf:all-elim
           (pf:all-elim (pf:known sep-elim-prop) (Union (Union R)))
           (lam y (ex (lam x (In (kpair x y) R)))))
         y)
       h)))))
; relations: the singleton of a member of S is included in S
!(pf:theorem &thy sing-subq (all set (lam x (all set (lam S (imp (In x S) (Subq (Sing x) S))))))
   (pf:fix x (pf:fix S (pf:assume hx (pf:fix w (pf:assume hw
     (pf:eq-subst
       (lam m (In m S))
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:known eq-sym) w) x)
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known sing-elim) x) w) hw))
       hx)))))))
; relations: the pair of two members of S is included in S
!(pf:theorem &thy upair-subq
   (all set
     (lam x (all set (lam y (all set (lam S (imp (In x S) (imp (In y S) (Subq (UPair x y) S)))))))))
   (pf:fix x (pf:fix y (pf:fix S (pf:assume hx (pf:assume hy (pf:fix w (pf:assume hw
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-elim) x) y) w) hw)
           (In w S))
         (pf:assume hwx
           (pf:eq-subst
             (lam m (In m S))
             (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym) w) x) hwx)
             hx)))
       (pf:assume hwy
         (pf:eq-subst
           (lam m (In m S))
           (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym) w) y) hwy)
           hy)))))))))))
; relations: an ordered pair of members of S is a member of Power (Power S)
!(pf:theorem &thy kpair-power
   (all set
     (lam x
       (all set
         (lam y (all set (lam S (imp (In x S) (imp (In y S) (In (kpair x y) (Power (Power S)))))))))))
   (pf:fix x (pf:fix y (pf:fix S (pf:assume hx (pf:assume hy
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known power-intro) (Power S)) (kpair x y))
       (pf:imp-elim
         (pf:imp-elim
           (pf:all-elim
             (pf:all-elim (pf:all-elim (pf:known upair-subq) (Sing x)) (UPair x y))
             (Power S))
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:known power-intro) S) (Sing x))
             (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known sing-subq) x) S) hx)))
         (pf:imp-elim
           (pf:all-elim (pf:all-elim (pf:known power-intro) S) (UPair x y))
           (pf:imp-elim
             (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known upair-subq) x) y) S) hx)
             hy))))))))))
; relations: the pair of a member of A and a member of B is a member of the product
!(pf:theorem &thy prod-intro
   (all set
     (lam A
       (all set
         (lam B
           (all set (lam x (all set (lam y (imp (In x A) (imp (In y B) (In (kpair x y) (prod A B))))))))))))
   (pf:fix A (pf:fix B (pf:fix x (pf:fix y (pf:assume hx (pf:assume hy
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:all-elim
             (pf:all-elim (pf:known sep-intro) (Power (Power (binunion A B))))
             (lam z (ex (lam x (and (In x A) (ex (lam y (and (In y B) (eq set z (kpair x y))))))))))
           (kpair x y))
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known kpair-power) x) y) (binunion A B))
             (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known binunion-left) A) B) x) hx))
           (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known binunion-right) A) B) y) hy)))
       (pf:fix p7 (pf:assume k8
         (pf:imp-elim
           (pf:all-elim k8 x)
           (pf:fix p5 (pf:assume k6
             (pf:imp-elim
               (pf:imp-elim k6 hx)
               (pf:fix p3 (pf:assume k4
                 (pf:imp-elim
                   (pf:all-elim k4 y)
                   (pf:fix p1 (pf:assume k2
                     (pf:imp-elim (pf:imp-elim k2 hy) (pf:all-elim (pf:known eq-refl) (kpair x y))))))))))))))))))))))
; relations: and the product has no other members
!(pf:theorem &thy prod-elim
   (all set
     (lam A
       (all set
         (lam B
           (all set
             (lam z
               (imp
                 (In z (prod A B))
                 (ex (lam x (and (In x A) (ex (lam y (and (In y B) (eq set z (kpair x y)))))))))))))))
   (pf:fix A (pf:fix B (pf:fix z (pf:assume h
     (pf:imp-elim
       (pf:all-elim
         (pf:all-elim
           (pf:all-elim (pf:known sep-elim-prop) (Power (Power (binunion A B))))
           (lam z (ex (lam x (and (In x A) (ex (lam y (and (In y B) (eq set z (kpair x y))))))))))
         z)
       h))))))
; relations: a product is a relation
!(pf:theorem &thy prod-relation (all set (lam A (all set (lam B (relation (prod A B))))))
   (pf:fix A (pf:fix B (pf:fix z (pf:assume hz
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known prod-elim) A) B) z) hz)
         (ex (lam x (ex (lam y (eq set z (kpair x y)))))))
       (pf:fix x (pf:assume hx
         (pf:imp-elim
           (pf:all-elim
             (pf:imp-elim
               (pf:all-elim hx (ex (lam y (and (In y B) (eq set z (kpair x y))))))
               (pf:assume l1 (pf:assume r2 r2)))
             (ex (lam x (ex (lam y (eq set z (kpair x y)))))))
           (pf:fix y (pf:assume hy (pf:fix p7 (pf:assume k8
             (pf:imp-elim
               (pf:all-elim k8 x)
               (pf:fix p5 (pf:assume k6
                 (pf:imp-elim
                   (pf:all-elim k6 y)
                   (pf:imp-elim (pf:all-elim hy (eq set z (kpair x y))) (pf:assume l3 (pf:assume r4 r4))))))))))))))))))))
; relations: a subset of a relation is a relation
!(pf:theorem &thy relation-subq
   (all set (lam R (all set (lam S (imp (relation R) (imp (Subq S R) (relation S)))))))
   (pf:fix R (pf:fix S (pf:assume hR (pf:assume hS (pf:fix z (pf:assume hz
     (pf:imp-elim (pf:all-elim hR z) (pf:imp-elim (pf:all-elim hS z) hz)))))))))
; relations: the domain of a product of A and B is included in A
!(pf:theorem &thy dom-prod (all set (lam A (all set (lam B (Subq (dom (prod A B)) A)))))
   (pf:fix A (pf:fix B (pf:fix x (pf:assume hx
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known dom-elim) (prod A B)) x) hx)
         (In x A))
       (pf:fix y (pf:assume hy
         (pf:imp-elim
           (pf:all-elim
             (pf:imp-elim
               (pf:all-elim (pf:all-elim (pf:all-elim (pf:known prod-elim) A) B) (kpair x y))
               hy)
             (In x A))
           (pf:fix u (pf:assume hu
             (pf:imp-elim
               (pf:all-elim
                 (pf:imp-elim
                   (pf:all-elim hu (ex (lam y2 (and (In y2 B) (eq set (kpair x y) (kpair u y2))))))
                   (pf:assume l1 (pf:assume r2 r2)))
                 (In x A))
               (pf:fix v (pf:assume hv
                 (pf:eq-subst
                   (lam m (In m A))
                   (pf:imp-elim
                     (pf:all-elim (pf:all-elim (pf:known eq-sym) x) u)
                     (pf:imp-elim
                       (pf:all-elim
                         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known kpair-inj-first) x) y) u)
                         v)
                       (pf:imp-elim
                         (pf:all-elim hv (eq set (kpair x y) (kpair u v)))
                         (pf:assume l3 (pf:assume r4 r4)))))
                   (pf:imp-elim (pf:all-elim hu (In u A)) (pf:assume l5 (pf:assume r6 l5))))))))))))))))))

; ---- functions (Naproche-ZF library: functions): a function from A to B is a
; functional and total relation inside the product; a surjection reaches every
; member of B
!(set:define &thy functional (-> set prop)
   (= (functional $f)
     (all set
       (lam x
         (all set
           (lam y (all set (lam z (imp (In (kpair x y) $f) (imp (In (kpair x z) $f) (eq set y z)))))))))))
!(set:define &thy total (-> set set prop)
   (= (total $f $A) (all set (lam x (imp (In x $A) (ex (lam y (In (kpair x y) $f))))))))
!(set:define &thy function (-> set set set prop)
   (= (function $f $A $B) (and (Subq $f (prod $A $B)) (and (functional $f) (total $f $A)))))
!(set:define &thy onto (-> set set set prop)
   (= (onto $f $A $B)
     (all set (lam y (imp (In y $B) (ex (lam x (and (In x $A) (In (kpair x y) $f)))))))))
!(set:define &thy surjection (-> set set set prop)
   (= (surjection $f $A $B) (and (function $f $A $B) (onto $f $A $B))))
; the identity relation on A: the pairs of a member of A with itself
!(set:define &thy id-rel (-> set set)
   (= (id-rel $A) (Sep (prod $A $A) (lam z (ex (lam x (eq set z (kpair x x))))))))
; functions: a function from A is defined on all of A
!(pf:theorem &thy function-dom
   (all set (lam f (all set (lam A (all set (lam B (imp (function f A B) (Subq A (dom f)))))))))
   (pf:fix f (pf:fix A (pf:fix B (pf:assume h (pf:fix x (pf:assume hx
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim
           (pf:all-elim
             (pf:imp-elim
               (pf:all-elim
                 (pf:imp-elim
                   (pf:all-elim h (and (functional f) (total f A)))
                   (pf:assume l1 (pf:assume r2 r2)))
                 (total f A))
               (pf:assume l3 (pf:assume r4 r4)))
             x)
           hx)
         (In x (dom f)))
       (pf:fix y (pf:assume hy
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known dom-intro) f) x) y) hy)))))))))))
; functions: the identity relation on A is a function from A to A
!(pf:theorem &thy id-function (all set (lam A (function (id-rel A) A A)))
   (pf:fix A (pf:fix p7 (pf:assume k8
     (pf:imp-elim
       (pf:imp-elim k8
         (pf:all-elim
           (pf:all-elim (pf:known sep-subq) (prod A A))
           (lam z (ex (lam x (eq set z (kpair x x)))))))
       (pf:fix p5 (pf:assume k6
         (pf:imp-elim
           (pf:imp-elim k6
             (pf:fix x (pf:fix y (pf:fix z (pf:assume hy (pf:assume hz
               (pf:imp-elim
                 (pf:all-elim
                   (pf:imp-elim
                     (pf:all-elim
                       (pf:all-elim
                         (pf:all-elim (pf:known sep-elim-prop) (prod A A))
                         (lam z (ex (lam x (eq set z (kpair x x))))))
                       (kpair x y))
                     hy)
                   (eq set y z))
                 (pf:fix u (pf:assume hu
                   (pf:imp-elim
                     (pf:all-elim
                       (pf:imp-elim
                         (pf:all-elim
                           (pf:all-elim
                             (pf:all-elim (pf:known sep-elim-prop) (prod A A))
                             (lam z (ex (lam x (eq set z (kpair x x))))))
                           (kpair x z))
                         hz)
                       (eq set y z))
                     (pf:fix v (pf:assume hv
                       (pf:imp-elim
                         (pf:imp-elim
                           (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans) y) u) z)
                           (pf:imp-elim
                             (pf:all-elim
                               (pf:all-elim
                                 (pf:all-elim (pf:all-elim (pf:known kpair-inj-second) x) y)
                                 u)
                               u)
                             hu))
                         (pf:imp-elim
                           (pf:imp-elim
                             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans) u) x) z)
                             (pf:imp-elim
                               (pf:all-elim (pf:all-elim (pf:known eq-sym) x) u)
                               (pf:imp-elim
                                 (pf:all-elim
                                   (pf:all-elim
                                     (pf:all-elim (pf:all-elim (pf:known kpair-inj-first) x) y)
                                     u)
                                   u)
                                 hu)))
                           (pf:imp-elim
                             (pf:imp-elim
                               (pf:all-elim (pf:all-elim (pf:all-elim (pf:known eq-trans) x) v) z)
                               (pf:imp-elim
                                 (pf:all-elim
                                   (pf:all-elim
                                     (pf:all-elim (pf:all-elim (pf:known kpair-inj-first) x) z)
                                     v)
                                   v)
                                 hv))
                             (pf:imp-elim
                               (pf:all-elim (pf:all-elim (pf:known eq-sym) z) v)
                               (pf:imp-elim
                                 (pf:all-elim
                                   (pf:all-elim
                                     (pf:all-elim (pf:all-elim (pf:known kpair-inj-second) x) z)
                                     v)
                                   v)
                                 hv)))))))))))))))))
           (pf:fix x (pf:assume hx (pf:fix p3 (pf:assume k4
             (pf:imp-elim
               (pf:all-elim k4 x)
               (pf:imp-elim
                 (pf:imp-elim
                   (pf:all-elim
                     (pf:all-elim
                       (pf:all-elim (pf:known sep-intro) (prod A A))
                       (lam z (ex (lam x (eq set z (kpair x x))))))
                     (kpair x x))
                   (pf:imp-elim
                     (pf:imp-elim
                       (pf:all-elim
                         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known prod-intro) A) A) x)
                         x)
                       hx)
                     hx))
                 (pf:fix p1 (pf:assume k2
                   (pf:imp-elim (pf:all-elim k2 x) (pf:all-elim (pf:known eq-refl) (kpair x x)))))))))))))))))))
; functions (Cantor): no functional relation maps A onto its power set
; The diagonal set D holds the members x of A outside every value at x;
; the value D at some a would put a in D exactly when a is not in D.
!(pf:theorem &thy cantor-core
   (all set (lam A (all set (lam f (imp (functional f) (imp (onto f A (Power A)) Falsum))))))
   (pf:fix A (pf:fix f (pf:assume hf (pf:assume ho
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim
           (pf:all-elim ho (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
           (pf:imp-elim
             (pf:all-elim
               (pf:all-elim (pf:known power-intro) A)
               (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
             (pf:all-elim
               (pf:all-elim (pf:known sep-subq) A)
               (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y)))))))))
         Falsum)
       (pf:fix a (pf:assume ha
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim
               (pf:imp-elim
                 (pf:all-elim
                   (pf:all-elim
                     (pf:all-elim (pf:known sep-elim-prop) A)
                     (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y)))))))
                   a)
                 (pf:imp-elim
                   (pf:imp-elim
                     (pf:all-elim
                       (pf:all-elim
                         (pf:all-elim (pf:known sep-intro) A)
                         (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y)))))))
                       a)
                     (pf:imp-elim (pf:all-elim ha (In a A)) (pf:assume l1 (pf:assume r2 l1))))
                   (pf:fix Y (pf:assume hY (pf:assume aY
                     (pf:imp-elim
                       (pf:imp-elim
                         (pf:all-elim
                           (pf:imp-elim
                             (pf:all-elim
                               (pf:all-elim
                                 (pf:all-elim (pf:known sep-elim-prop) A)
                                 (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y)))))))
                               a)
                             (pf:eq-subst
                               (lam m (In a m))
                               (pf:imp-elim
                                 (pf:imp-elim
                                   (pf:all-elim
                                     (pf:all-elim (pf:all-elim hf a) Y)
                                     (Sep A
                                       (lam x
                                         (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                                   hY)
                                 (pf:imp-elim
                                   (pf:all-elim ha
                                     (In
                                       (kpair a
                                         (Sep A
                                           (lam x
                                             (all set
                                               (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                                       f))
                                   (pf:assume l3 (pf:assume r4 r4))))
                               aY))
                           (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                         (pf:imp-elim
                           (pf:all-elim ha
                             (In
                               (kpair a
                                 (Sep A
                                   (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                               f))
                           (pf:assume l5 (pf:assume r6 r6))))
                       (pf:eq-subst
                         (lam m (In a m))
                         (pf:imp-elim
                           (pf:imp-elim
                             (pf:all-elim
                               (pf:all-elim (pf:all-elim hf a) Y)
                               (Sep A
                                 (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                             hY)
                           (pf:imp-elim
                             (pf:all-elim ha
                               (In
                                 (kpair a
                                   (Sep A
                                     (lam x
                                       (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                                 f))
                             (pf:assume l7 (pf:assume r8 r8))))
                         aY)))))))
               (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
             (pf:imp-elim
               (pf:all-elim ha
                 (In
                   (kpair a (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                   f))
               (pf:assume l9 (pf:assume r10 r10))))
           (pf:imp-elim
             (pf:imp-elim
               (pf:all-elim
                 (pf:all-elim
                   (pf:all-elim (pf:known sep-intro) A)
                   (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y)))))))
                 a)
               (pf:imp-elim (pf:all-elim ha (In a A)) (pf:assume l11 (pf:assume r12 l11))))
             (pf:fix Y (pf:assume hY (pf:assume aY
               (pf:imp-elim
                 (pf:imp-elim
                   (pf:all-elim
                     (pf:imp-elim
                       (pf:all-elim
                         (pf:all-elim
                           (pf:all-elim (pf:known sep-elim-prop) A)
                           (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y)))))))
                         a)
                       (pf:eq-subst
                         (lam m (In a m))
                         (pf:imp-elim
                           (pf:imp-elim
                             (pf:all-elim
                               (pf:all-elim (pf:all-elim hf a) Y)
                               (Sep A
                                 (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                             hY)
                           (pf:imp-elim
                             (pf:all-elim ha
                               (In
                                 (kpair a
                                   (Sep A
                                     (lam x
                                       (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                                 f))
                             (pf:assume l13 (pf:assume r14 r14))))
                         aY))
                     (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                   (pf:imp-elim
                     (pf:all-elim ha
                       (In
                         (kpair a
                           (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                         f))
                     (pf:assume l15 (pf:assume r16 r16))))
                 (pf:eq-subst
                   (lam m (In a m))
                   (pf:imp-elim
                     (pf:imp-elim
                       (pf:all-elim
                         (pf:all-elim (pf:all-elim hf a) Y)
                         (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                       hY)
                     (pf:imp-elim
                       (pf:all-elim ha
                         (In
                           (kpair a
                             (Sep A
                               (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                           f))
                       (pf:assume l17 (pf:assume r18 r18))))
                   aY)))))))))))))))
; functions (Cantor's theorem): there is no surjection from A onto its power set
!(pf:theorem &thy cantor (all set (lam A (not (ex (lam f (surjection f A (Power A)))))))
   (pf:fix A (pf:assume h
     (pf:imp-elim
       (pf:all-elim h Falsum)
       (pf:fix f (pf:assume hs
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:known cantor-core) A) f)
             (pf:imp-elim
               (pf:all-elim
                 (pf:imp-elim
                   (pf:all-elim
                     (pf:imp-elim
                       (pf:all-elim hs (function f A (Power A)))
                       (pf:assume l1 (pf:assume r2 l1)))
                     (and (functional f) (total f A)))
                   (pf:assume l3 (pf:assume r4 r4)))
                 (functional f))
               (pf:assume l5 (pf:assume r6 l5))))
           (pf:imp-elim (pf:all-elim hs (onto f A (Power A))) (pf:assume l7 (pf:assume r8 r8))))))))))
; functions (Cantor): the diagonal argument for a function of the logic
!(pf:theorem &thy cantor-fn
   (all set
     (lam A
       (all
         (-> set set)
         (lam F
           (not (all set (lam Y (imp (In Y (Power A)) (ex (lam x (and (In x A) (eq set (F x) Y))))))))))))
   (pf:fix A (pf:fix F (pf:assume h
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim
           (pf:all-elim h (Sep A (lam x (not (In x (F x))))))
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:known power-intro) A) (Sep A (lam x (not (In x (F x))))))
             (pf:all-elim (pf:all-elim (pf:known sep-subq) A) (lam x (not (In x (F x)))))))
         Falsum)
       (pf:fix a (pf:assume ha
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim
               (pf:all-elim (pf:all-elim (pf:known sep-elim-prop) A) (lam x (not (In x (F x)))))
               a)
             (pf:imp-elim
               (pf:imp-elim
                 (pf:all-elim
                   (pf:all-elim (pf:all-elim (pf:known sep-intro) A) (lam x (not (In x (F x)))))
                   a)
                 (pf:imp-elim (pf:all-elim ha (In a A)) (pf:assume l1 (pf:assume r2 l1))))
               (pf:assume u
                 (pf:imp-elim
                   (pf:imp-elim
                     (pf:all-elim
                       (pf:all-elim (pf:all-elim (pf:known sep-elim-prop) A) (lam x (not (In x (F x)))))
                       a)
                     (pf:eq-subst
                       (lam m (In a m))
                       (pf:imp-elim
                         (pf:all-elim ha (eq set (F a) (Sep A (lam x (not (In x (F x)))))))
                         (pf:assume l3 (pf:assume r4 r4)))
                       u))
                   u))))
           (pf:eq-subst
             (lam m (In a m))
             (pf:imp-elim
               (pf:all-elim
                 (pf:all-elim (pf:known eq-sym) (F a))
                 (Sep A (lam x (not (In x (F x))))))
               (pf:imp-elim
                 (pf:all-elim ha (eq set (F a) (Sep A (lam x (not (In x (F x)))))))
                 (pf:assume l5 (pf:assume r6 r6))))
             (pf:imp-elim
               (pf:imp-elim
                 (pf:all-elim
                   (pf:all-elim (pf:all-elim (pf:known sep-intro) A) (lam x (not (In x (F x)))))
                   a)
                 (pf:imp-elim (pf:all-elim ha (In a A)) (pf:assume l7 (pf:assume r8 l7))))
               (pf:assume u
                 (pf:imp-elim
                   (pf:imp-elim
                     (pf:all-elim
                       (pf:all-elim
                         (pf:all-elim (pf:known sep-elim-prop) A)
                         (lam x (not (In x (F x)))))
                       a)
                     (pf:eq-subst
                       (lam m (In a m))
                       (pf:imp-elim
                         (pf:all-elim ha (eq set (F a) (Sep A (lam x (not (In x (F x)))))))
                         (pf:assume l9 (pf:assume r10 r10)))
                       u))
                   u))))))))))))

; ---- ordinals (Naproche-ZF library: ordinals): an ordinal is a transitive set
; whose members are transitive sets, the definition Naproche-ZF and Megalodon
; use.  Membership is well-founded by the law of induction on membership, so
; this definition needs no well-ordering clause; that membership is a linear
; order of the ordinals is classical and is not claimed here.
!(set:define &thy TransSet (-> set prop)
   (= (TransSet $X) (all set (lam x (imp (In x $X) (Subq x $X))))))
!(set:define &thy ordinal (-> set prop)
   (= (ordinal $a) (and (TransSet $a) (all set (lam b (imp (In b $a) (TransSet b)))))))
!(set:define &thy succ (-> set set) (= (succ $a) (binunion $a (Sing $a))))
; ordinals (transitive sets): the power set of a transitive set is transitive
!(pf:theorem &thy transset-power (all set (lam A (imp (TransSet A) (TransSet (Power A)))))
   (pf:fix A (pf:assume hA (pf:fix x (pf:assume hx (pf:fix y (pf:assume hy
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:known power-intro) A) y)
       (pf:imp-elim
         (pf:all-elim hA y)
         (pf:imp-elim
           (pf:all-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known power-elim) A) x) hx) y)
           hy))))))))))
; ordinals (transitive sets): the union of a set of transitive sets is transitive
!(pf:theorem &thy transset-union
   (all set (lam S (imp (all set (lam X (imp (In X S) (TransSet X)))) (TransSet (Union S)))))
   (pf:fix S (pf:assume hS (pf:fix x (pf:assume hx (pf:fix y (pf:assume hy
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known union-elim) S) x) hx)
         (In y (Union S)))
       (pf:fix X (pf:assume hXS (pf:assume hxX
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known union-intro) S) y) X)
             (pf:imp-elim
               (pf:all-elim (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim hS X) hXS) x) hxX) y)
               hy))
           hXS))))))))))))
; ordinals (transitive sets): the universe of a set is transitive, by its law
!(pf:theorem &thy univ-transset (all set (lam N (TransSet (UnivOf N))))
   (pf:fix N (pf:fix x (pf:assume hx (pf:fix z (pf:assume hz
     (pf:imp-elim
       (pf:all-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known universeTransitive) N) x) hx) z)
       hz)))))))
; ordinals: an ordinal is a transitive set
!(pf:theorem &thy ordinal-transset (all set (lam a (imp (ordinal a) (TransSet a))))
   (pf:fix a (pf:assume h (pf:imp-elim (pf:all-elim h (TransSet a)) (pf:assume l1 (pf:assume r2 l1))))))
; ordinals: whose members are transitive sets
!(pf:theorem &thy ordinal-member-transset
   (all set (lam a (all set (lam b (imp (ordinal a) (imp (In b a) (TransSet b)))))))
   (pf:fix a (pf:fix b (pf:assume h (pf:assume hb
     (pf:imp-elim
       (pf:all-elim
         (pf:imp-elim
           (pf:all-elim h (all set (lam b (imp (In b a) (TransSet b)))))
           (pf:assume l1 (pf:assume r2 r2)))
         b)
       hb))))))
; ordinals: the empty set is an ordinal
!(pf:theorem &thy empty-ordinal (ordinal Empty)
   (pf:fix p1 (pf:assume k2
     (pf:imp-elim
       (pf:imp-elim k2
         (pf:fix x (pf:assume h
           (pf:all-elim (pf:imp-elim (pf:all-elim (pf:known emptyLaw) x) h) (Subq x Empty)))))
       (pf:fix b (pf:assume h
         (pf:all-elim (pf:imp-elim (pf:all-elim (pf:known emptyLaw) b) h) (TransSet b))))))))
; ordinals: every member of an ordinal is an ordinal
!(pf:theorem &thy ordinal-hered
   (all set (lam a (all set (lam b (imp (ordinal a) (imp (In b a) (ordinal b)))))))
   (pf:fix a (pf:fix b (pf:assume ha (pf:assume hb (pf:fix p1 (pf:assume k2
     (pf:imp-elim
       (pf:imp-elim k2
         (pf:imp-elim
           (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known ordinal-member-transset) a) b) ha)
           hb))
       (pf:fix c (pf:assume hc
         (pf:imp-elim
           (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known ordinal-member-transset) a) c) ha)
           (pf:imp-elim
             (pf:all-elim
               (pf:imp-elim
                 (pf:all-elim (pf:imp-elim (pf:all-elim (pf:known ordinal-transset) a) ha) b)
                 hb)
               c)
             hc))))))))))))
; ordinals (Burali-Forti): there is no set of all ordinals
; Such a set would be transitive with transitive members, an ordinal,
; and so a member of itself.
!(pf:theorem &thy burali-forti (not (ex (lam O (all set (lam a (iff (In a O) (ordinal a)))))))
   (pf:assume h
     (pf:imp-elim
       (pf:all-elim h Falsum)
       (pf:fix O (pf:assume hO
         (pf:imp-elim
           (pf:all-elim (pf:known in-irrefl) O)
           (pf:imp-elim
             (pf:imp-elim
               (pf:all-elim (pf:all-elim hO O) (imp (ordinal O) (In O O)))
               (pf:assume l9 (pf:assume r10 r10)))
             (pf:fix p7 (pf:assume k8
               (pf:imp-elim
                 (pf:imp-elim k8
                   (pf:fix x (pf:assume hx (pf:fix y (pf:assume hy
                     (pf:imp-elim
                       (pf:imp-elim
                         (pf:all-elim (pf:all-elim hO y) (imp (ordinal y) (In y O)))
                         (pf:assume l3 (pf:assume r4 r4)))
                       (pf:imp-elim
                         (pf:imp-elim
                           (pf:all-elim (pf:all-elim (pf:known ordinal-hered) x) y)
                           (pf:imp-elim
                             (pf:imp-elim
                               (pf:all-elim (pf:all-elim hO x) (imp (In x O) (ordinal x)))
                               (pf:assume l1 (pf:assume r2 l1)))
                             hx))
                         hy)))))))
                 (pf:fix b (pf:assume hb
                   (pf:imp-elim
                     (pf:all-elim (pf:known ordinal-transset) b)
                     (pf:imp-elim
                       (pf:imp-elim
                         (pf:all-elim (pf:all-elim hO b) (imp (In b O) (ordinal b)))
                         (pf:assume l5 (pf:assume r6 l5)))
                       hb))))))))))))))
; ordinals: the members of the successor of a are the members of a and a itself
!(pf:theorem &thy succ-elim
   (all set (lam a (all set (lam x (imp (In x (succ a)) (or (In x a) (eq set x a)))))))
   (pf:fix a (pf:fix x (pf:assume h
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known binunion-elim) a) (Sing a)) x)
             h)
           (or (In x a) (eq set x a)))
         (pf:assume h1 (pf:fix p1 (pf:assume l2 (pf:assume r3 (pf:imp-elim l2 h1))))))
       (pf:assume h2 (pf:fix p4 (pf:assume l5 (pf:assume r6
         (pf:imp-elim r6 (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known sing-elim) a) x) h2)))))))))))
; ordinals: a is a member of its successor
!(pf:theorem &thy succ-self (all set (lam a (In a (succ a))))
   (pf:fix a
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known binunion-right) a) (Sing a)) a)
       (pf:all-elim (pf:known sing-intro) a))))
; ordinals: and so is every member of a
!(pf:theorem &thy succ-mem (all set (lam a (all set (lam x (imp (In x a) (In x (succ a)))))))
   (pf:fix a (pf:fix x (pf:assume h
     (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known binunion-left) a) (Sing a)) x) h)))))
; ordinals: the successor of an ordinal is an ordinal
!(pf:theorem &thy ordinal-succ (all set (lam a (imp (ordinal a) (ordinal (succ a)))))
   (pf:fix a (pf:assume ha (pf:fix p1 (pf:assume k2
     (pf:imp-elim
       (pf:imp-elim k2
         (pf:fix x (pf:assume hx (pf:fix y (pf:assume hy
           (pf:imp-elim
             (pf:imp-elim
               (pf:all-elim
                 (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known succ-elim) a) x) hx)
                 (In y (succ a)))
               (pf:assume hxa
                 (pf:imp-elim
                   (pf:all-elim (pf:all-elim (pf:known succ-mem) a) y)
                   (pf:imp-elim
                     (pf:all-elim
                       (pf:imp-elim
                         (pf:all-elim (pf:imp-elim (pf:all-elim (pf:known ordinal-transset) a) ha) x)
                         hxa)
                       y)
                     hy))))
             (pf:assume hxs
               (pf:imp-elim
                 (pf:all-elim (pf:all-elim (pf:known succ-mem) a) y)
                 (pf:eq-subst (lam m (In y m)) hxs hy)))))))))
       (pf:fix b (pf:assume hb
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim
               (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known succ-elim) a) b) hb)
               (TransSet b))
             (pf:assume hba
               (pf:imp-elim
                 (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known ordinal-member-transset) a) b) ha)
                 hba)))
           (pf:assume hbs
             (pf:eq-subst
               (lam m (TransSet m))
               (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym) b) a) hbs)
               (pf:imp-elim (pf:all-elim (pf:known ordinal-transset) a) ha))))))))))))
; ordinals: a set is not the same as its successor
!(pf:theorem &thy succ-neq (all set (lam a (not (eq set (succ a) a))))
   (pf:fix a (pf:assume h
     (pf:imp-elim
       (pf:all-elim (pf:known in-irrefl) a)
       (pf:eq-subst (lam m (In a m)) h (pf:all-elim (pf:known succ-self) a))))))
; ordinals: sets with the same successor are the same
!(pf:theorem &thy succ-inj (all set (lam a (all set (lam b (imp (eq set (succ a) (succ b)) (eq set a b))))))
   (pf:fix a (pf:fix b (pf:assume h
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:known succ-elim) b) a)
             (pf:eq-subst (lam m (In a m)) h (pf:all-elim (pf:known succ-self) a)))
           (eq set a b))
         (pf:assume hab
           (pf:imp-elim
             (pf:imp-elim
               (pf:all-elim
                 (pf:imp-elim
                   (pf:all-elim (pf:all-elim (pf:known succ-elim) a) b)
                   (pf:eq-subst
                     (lam m (In b m))
                     (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym) (succ a)) (succ b)) h)
                     (pf:all-elim (pf:known succ-self) b)))
                 (eq set a b))
               (pf:assume hba
                 (pf:all-elim
                   (pf:imp-elim
                     (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known in-asym) a) b) hab)
                     hba)
                   (eq set a b))))
             (pf:assume hsb (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known eq-sym) b) a) hsb)))))
       (pf:assume hsa hsa))))))
; ordinals: the union of a set of ordinals is an ordinal
!(pf:theorem &thy ordinal-union
   (all set (lam S (imp (all set (lam a (imp (In a S) (ordinal a)))) (ordinal (Union S)))))
   (pf:fix S (pf:assume hS (pf:fix p1 (pf:assume k2
     (pf:imp-elim
       (pf:imp-elim k2
         (pf:fix x (pf:assume hx (pf:fix y (pf:assume hy
           (pf:imp-elim
             (pf:all-elim
               (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known union-elim) S) x) hx)
               (In y (Union S)))
             (pf:fix a (pf:assume haS (pf:assume hxa
               (pf:imp-elim
                 (pf:imp-elim
                   (pf:all-elim (pf:all-elim (pf:all-elim (pf:known union-intro) S) y) a)
                   (pf:imp-elim
                     (pf:all-elim
                       (pf:imp-elim
                         (pf:all-elim
                           (pf:imp-elim
                             (pf:all-elim (pf:known ordinal-transset) a)
                             (pf:imp-elim (pf:all-elim hS a) haS))
                           x)
                         hxa)
                       y)
                     hy))
                 haS))))))))))
       (pf:fix b (pf:assume hb
         (pf:imp-elim
           (pf:all-elim
             (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known union-elim) S) b) hb)
             (TransSet b))
           (pf:fix a (pf:assume haS (pf:assume hba
             (pf:imp-elim
               (pf:imp-elim
                 (pf:all-elim (pf:all-elim (pf:known ordinal-member-transset) a) b)
                 (pf:imp-elim (pf:all-elim hS a) haS))
               hba)))))))))))))
; ordinals: induction along membership inside a transitive set, as an ordinal
!(pf:theorem &thy transset-induction
   (all set
     (lam a
       (imp
         (TransSet a)
         (all
           (-> set prop)
           (lam P
             (imp
               (all set (lam x (imp (In x a) (imp (all set (lam y (imp (In y x) (P y)))) (P x)))))
               (all set (lam x (imp (In x a) (P x))))))))))
   (pf:fix a (pf:assume ht (pf:fix P (pf:assume hstep
     (pf:imp-elim
       (pf:all-elim (pf:known setInduction) (lam x (imp (In x a) (P x))))
       (pf:fix X (pf:assume ih (pf:assume hXa
         (pf:imp-elim
           (pf:imp-elim (pf:all-elim hstep X) hXa)
           (pf:fix y (pf:assume hyX
             (pf:imp-elim
               (pf:imp-elim (pf:all-elim ih y) hyX)
               (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim ht X) hXa) y) hyX))))))))))))))

; ---- orders (Naproche-ZF library: orders): a strict order on a set, for a
; relation of the logic; membership is one on every ordinal
!(set:define &thy irreflexive-on (-> (-> set set prop) set prop)
   (= (irreflexive-on $R $A) (all set (lam x (imp (In x $A) (not ($R x x)))))))
!(set:define &thy transitive-on (-> (-> set set prop) set prop)
   (= (transitive-on $R $A)
     (all set
       (lam x
         (all set
           (lam y
             (all set
               (lam z
                 (imp (In x $A) (imp (In y $A) (imp (In z $A) (imp ($R x y) (imp ($R y z) ($R x z))))))))))))))
!(set:define &thy strict-order (-> (-> set set prop) set prop)
   (= (strict-order $R $A) (and (irreflexive-on $R $A) (transitive-on $R $A))))
; orders: membership, as the relation (lam x (lam y (In x y))) of the logic,
; is a strict order on every ordinal
!(pf:theorem &thy ordinal-strict-order
   (all set (lam a (imp (ordinal a) (strict-order (lam x (lam y (In x y))) a))))
   (pf:fix a (pf:assume ha (pf:fix p1 (pf:assume k2
     (pf:imp-elim
       (pf:imp-elim k2 (pf:fix x (pf:assume hx (pf:all-elim (pf:known in-irrefl) x))))
       (pf:fix x (pf:fix y (pf:fix z (pf:assume hx (pf:assume hy (pf:assume hz (pf:assume hxy (pf:assume hyz
         (pf:imp-elim
           (pf:all-elim
             (pf:imp-elim
               (pf:all-elim
                 (pf:imp-elim
                   (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known ordinal-member-transset) a) z) ha)
                   hz)
                 y)
               hyz)
             x)
           hxy)))))))))))))))
; orders: a strict order is asymmetric
!(pf:theorem &thy strict-order-asym
   (all
     (-> set set prop)
     (lam R
       (all set
         (lam A
           (imp
             (strict-order R A)
             (all set
               (lam x (all set (lam y (imp (In x A) (imp (In y A) (imp (R x y) (not (R y x))))))))))))))
   (pf:fix R (pf:fix A (pf:assume h (pf:fix x (pf:fix y (pf:assume hx (pf:assume hy (pf:assume hxy (pf:assume hyx
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim
           (pf:imp-elim (pf:all-elim h (irreflexive-on R A)) (pf:assume l1 (pf:assume r2 l1)))
           x)
         hx)
       (pf:imp-elim
         (pf:imp-elim
           (pf:imp-elim
             (pf:imp-elim
               (pf:imp-elim
                 (pf:all-elim
                   (pf:all-elim
                     (pf:all-elim
                       (pf:imp-elim
                         (pf:all-elim h (transitive-on R A))
                         (pf:assume l3 (pf:assume r4 r4)))
                       x)
                     y)
                   x)
                 hx)
               hy)
             hx)
           hxy)
         hyx))))))))))))

; ---- controls: proofs that do not prove other statements
; Russell's argument alone does not refute a set of all sets: that needs
; separation.
!(pf:proves &thy
   (pf:assume h
     (pf:imp-elim
       (pf:all-elim h Falsum)
       (pf:fix R (pf:assume hR
         (pf:imp-elim (pf:all-elim (pf:known iff-not-absurd) (In R R)) (pf:all-elim hR R))))))
   (not (ex (lam V (all set (lam x (In x V)))))))
; The congruence of pairs does not exchange their members.
!(pf:proves &thy (pf:known kpair-cong) (all set (lam a (all set (lam b (eq set (kpair a b) (kpair b a)))))))
; Induction on membership does not make membership reflexive: in-irrefl proves
; the opposite.
!(pf:proves &thy (pf:known in-irrefl) (all set (lam x (In x x))))

; ---- what the rule of substitution adds
; An equation is now an identity, and what the curriculum beside this one
; refuses at its end is proved.  An equation moves a membership:
!(pf:theorem &thy eq-in-empty
   (all set (lam x (all set (lam y (imp (eq set x y) (imp (In x Empty) (In y Empty)))))))
   (pf:fix x (pf:fix y (pf:assume e (pf:assume h (pf:eq-subst (lam m (In m Empty)) e h))))))
; and sets with the same members are identical: every property of the one is
; a property of the other (Naproche-ZF: "by set extensionality").
!(pf:theorem &thy set-ext-identity
   (all set
     (lam X
       (all set
         (lam Y (imp (Subq X Y) (imp (Subq Y X) (all (-> set prop) (lam P (imp (P X) (P Y))))))))))
   (pf:fix X (pf:fix Y (pf:assume xy (pf:assume yx (pf:fix P (pf:assume h
     (pf:eq-subst P
       (pf:imp-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known set-ext) X) Y) xy) yx)
       h))))))))
; Cantor's theorem is stated above with the signature's equation,
; (eq set (F x) Y) in cantor-fn, and its diagonal step rewrites with it.
; The rule is substitution, not a reading of eq as Leibniz's: an equation
; instantiated as if it were a universal is still refused.
!(let ($verdict $why)
      (try (set:proves &thy
        (quote (pf:fix x (pf:fix y (pf:assume e (pf:assume h (pf:imp-elim (pf:all-elim e (lam m (In m Empty))) h))))))
        (quote (all set (lam x (all set (lam y (imp (eq set x y) (imp (In x Empty) (In y Empty))))))))))
   (let $head (car-atom $why) ($verdict $head)))
; That membership is a linear order of the ordinals is not proved: it implies
; excluded middle (the ordinal of the members of {Empty} for which p holds,
; compared with {Empty}, decides p), which the logic does not provide and the
; curriculum does not assume.
; The relation's two arguments keep their places: the proof of
; ordinal-strict-order does not prove the same of the converse relation
; (lam x (lam y (In y x))).
!(let ($verdict $why)
      (try (set:proves &thy
        (quote
          (pf:fix a (pf:assume ha (pf:fix p1 (pf:assume k2
            (pf:imp-elim
              (pf:imp-elim k2 (pf:fix x (pf:assume hx (pf:all-elim (pf:known in-irrefl) x))))
              (pf:fix x (pf:fix y (pf:fix z (pf:assume hx (pf:assume hy (pf:assume hz (pf:assume hxy (pf:assume hyz
                (pf:imp-elim
                  (pf:all-elim
                    (pf:imp-elim
                      (pf:all-elim
                        (pf:imp-elim
                          (pf:imp-elim
                            (pf:all-elim (pf:all-elim (pf:known ordinal-member-transset) a) z)
                            ha)
                          hz)
                        y)
                      hyz)
                    x)
                  hxy)))))))))))))))
        (quote (all set (lam a (imp (ordinal a) (strict-order (lam x (lam y (In y x))) a)))))))
   (let $head (car-atom $why) ($verdict $head)))

; ---- the type face: the proof term of each theorem is its native package,
; compiled from the rules and checked by the regular kernel; the Megalodon
; export of every theorem above reads it.  A proof term cites an earlier
; theorem by name: its package declares a constant whose type is that
; theorem's statement, and the cited proof is not compiled again.  For the
; showcase theorems and the theorems of equality, what each proof term cites
; (the theorems and the laws its package declares) and the laws and the axiom
; it rests on through them; substitution is a rule constant of the package,
; not an assumption:
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy russell)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy russell) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy no-universal-set)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy no-universal-set) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy kpair-inj)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy kpair-inj) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy cantor)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy cantor) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy cantor-fn)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy cantor-fn) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy burali-forti)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy burali-forti) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy ordinal-succ)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy ordinal-succ) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy ordinal-union)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy ordinal-union) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy ordinal-strict-order)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy ordinal-strict-order) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy eq-sym)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy eq-sym) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy eq-trans)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy eq-trans) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy eq-in-empty)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy eq-in-empty) (type-face $name (cites $cites) (rests-on $rests)))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy set-ext-identity)
   (let $cites (map-atom $rows $r (car-atom $r))
     (let $rests (pf:rests-on &thy set-ext-identity) (type-face $name (cites $cites) (rests-on $rests)))))

; ---- the set face: the truth of the statement in the set model.  Lean states
; it for three of the theorems above, which are cited with C's statement; it
; states none of the others yet.  The set of all sets is refuted in Lean's model
; of higher-order logic over its sets and in the set model of the tower package;
; that a set is a member of its power set holds in the latter; and no declared
; set is a member of itself, at every reading of the set-theory constants.
!(let (quote $p) (lift (set:known-proposition &thy no-universal-set))
   (quote (set-face no-universal-set $p
     "Mettapedia.Logic.HOL.Embedding.ZFSetHenkinInterpretation.universalSet_false"
     "TowerInterpretation.MegalodonHOTG.universalSet_false_in_model")))
!(let (quote $p) (lift (set:known-proposition &thy power-self))
   (quote (set-face power-self $p "TowerInterpretation.MegalodonHOTG.powerIn_holds")))
!(let (quote $p) (lift (set:known-proposition &thy in-irrefl))
   (quote (set-face in-irrefl $p (at-a-declared-set) "TowerInterpretation.MegalodonHOTG.selfMember_type_empty")))
