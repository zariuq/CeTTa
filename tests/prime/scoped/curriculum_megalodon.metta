; The Megalodon ladder (01-07): the logical constants as definitions, as the
; ladder writes them (True and False are MeTTa's Boolean atoms, so they are
; spelled truth and falsum here), the theorems about them, Leibniz equality with
; its symmetry, existence as its encoding, and the set-theoretic fragment:
; the signature carries In, Power, Union, Repl, Sep, Eps and UnivOf with
; three seed axioms; the ladder's remaining set axioms are declared here as
; axioms, as they are in the ladder's ambient theory.
!(import! &self pf)
!(bind! &thy (new-space))
!(set:define &thy truth prop (= truth (all prop (lam p (imp p p)))))
!(set:define &thy falsum prop (= falsum (all prop (lam p p))))
!(set:define &thy not (-> prop prop) (= (not $A) (imp $A falsum)))
!(set:define &thy and (-> prop prop prop) (= (and $A $B) (all prop (lam p (imp (imp $A (imp $B p)) p)))))
!(set:define &thy or (-> prop prop prop) (= (or $A $B) (all prop (lam p (imp (imp $A p) (imp (imp $B p) p))))))
!(set:define &thy iff (-> prop prop prop) (= (iff $A $B) (and (imp $A $B) (imp $B $A))))

; ---- 01 basics
!(pf:theorem &thy TrueI truth (pf:fix p (pf:assume h h)))
!(pf:theorem &thy andI (all prop (lam A (all prop (lam B (imp A (imp B (and A B)))))))
   (pf:fix A (pf:fix B (pf:assume a (pf:assume b (pf:fix p (pf:assume k (pf:by k a b))))))))
!(pf:theorem &thy andEL (all prop (lam A (all prop (lam B (imp (and A B) A)))))
   (pf:fix A (pf:fix B (pf:assume h (pf:by h (pf:assume a (pf:assume b a)))))))
!(pf:theorem &thy andER (all prop (lam A (all prop (lam B (imp (and A B) B)))))
   (pf:fix A (pf:fix B (pf:assume h (pf:by h (pf:assume a (pf:assume b b)))))))
!(pf:theorem &thy orIL (all prop (lam A (all prop (lam B (imp A (or A B))))))
   (pf:fix A (pf:fix B (pf:assume a (pf:fix p (pf:assume ka (pf:assume kb (pf:by ka a))))))))
!(pf:theorem &thy orIR (all prop (lam A (all prop (lam B (imp B (or A B))))))
   (pf:fix A (pf:fix B (pf:assume b (pf:fix p (pf:assume ka (pf:assume kb (pf:by kb b))))))))
!(pf:theorem &thy FalseE (imp falsum (all prop (lam p p)))
   (pf:assume h h))

; ---- 02 tactics: the same theorems with named steps, and iff
!(pf:theorem &thy iffI (all prop (lam A (all prop (lam B (imp (imp A B) (imp (imp B A) (iff A B)))))))
   (pf:fix A (pf:fix B (pf:assume f (pf:assume g (pf:fix p (pf:assume k (pf:by k f g))))))))
!(pf:theorem &thy test-let (all prop (lam A (all prop (lam B (imp A (imp B (and A B)))))))
   (pf:fix A (pf:fix B (pf:assume a (pf:assume b (pf:have ab (and A B) (pf:by andI a b) ab))))))

; ---- 03 sets: Leibniz equality on a type, its symmetry, existence
!(pf:theory &thy ((: A (u 0))))
!(set:define &thy leq (-> A A prop) (= (leq $x $y) (all (-> A A prop) (lam Q (imp (Q $x $y) (Q $y $x))))))
!(pf:theorem &thy leq-refl (all A (lam x (leq x x))) (pf:fix x (pf:fix Q (pf:assume h h))))
!(pf:theorem &thy leq-sym (all A (lam x (all A (lam y (imp (leq x y) (leq y x))))))
   (pf:fix x (pf:fix y (pf:assume h (pf:fix Q (pf:assume qyx
     (pf:imp-elim (pf:all-elim h (lam a (lam b (Q b a)))) qyx)))))))
!(set:define &thy ex (-> (-> A prop) prop) (= (ex $Q) (all prop (lam P (imp (all A (lam x (imp ($Q x) P))) P)))))
!(pf:theorem &thy exI (all (-> A prop) (lam Q (all A (lam x (imp (Q x) (ex Q))))))
   (pf:fix Q (pf:fix x (pf:assume q (pf:fix P (pf:assume k (pf:by k q)))))))
!(pf:theorem &thy exE (all (-> A prop) (lam Q (all prop (lam P (imp (ex Q) (imp (all A (lam x (imp (Q x) P))) P))))))
   (pf:fix Q (pf:fix P (pf:assume h (pf:assume k (pf:by h k))))))

; ---- 04-07 sets: the signature's set operations with the ladder's axioms
!(set:define &thy Subq (-> set set prop) (= (Subq $X $Y) (all set (lam x (imp (In x $X) (In x $Y))))))
!(pf:theorem &thy Subq-refl (all set (lam X (Subq X X))) (pf:fix X (pf:fix x (pf:assume h h))))
!(pf:theorem &thy Subq-trans (all set (lam X (all set (lam Y (all set (lam Z (imp (Subq X Y) (imp (Subq Y Z) (Subq X Z)))))))))
   (pf:fix X (pf:fix Y (pf:fix Z (pf:assume xy (pf:assume yz (pf:fix x (pf:assume h (pf:by yz (pf:by xy h))))))))))
!(pf:theorem &thy Power-self (all set (lam X (In X (Power X)))) (pf:fix X (pf:by PowerI)))
!(set:axiom &thy SepE (all set (lam X (all (-> set prop) (lam P (all set (lam x (imp (In x (Sep X P)) (In x X)))))))))
!(set:axiom &thy ReplI (all set (lam X (all (-> set set) (lam F (all set (lam x (imp (In x X) (In (F x) (Repl X F))))))))))
!(pf:theorem &thy sep-is-subq (all set (lam X (all (-> set prop) (lam P (Subq (Sep X P) X)))))
   (pf:fix X (pf:fix P (pf:fix x (pf:assume h (pf:by SepE h))))))
!(pf:theorem &thy repl-elem (all set (lam X (all (-> set set) (lam F (all set (lam x (imp (In x X) (In (F x) (Repl X F)))))))))
   (pf:fix X (pf:fix F (pf:fix x (pf:assume h (pf:by ReplI h))))))
!(set:axiom &thy EpsI (all (-> set prop) (lam P (all set (lam x (imp (P x) (P (Eps_set P))))))))
!(pf:theorem &thy choice-witness (all (-> set prop) (lam P (imp (all prop (lam R (imp (all set (lam x (imp (P x) R))) R))) (P (Eps_set P)))))
   (pf:fix P (pf:assume h (pf:by h (pf:fix x (pf:assume px (pf:by EpsI px)))))))
