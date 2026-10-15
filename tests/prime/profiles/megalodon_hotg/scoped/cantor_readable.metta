(set:profile megalodon-hotg)

; Cantor's theorem, its core, read sentence by sentence.  The Naproche-ZF
; curriculum proves cantor-core in the rules of the logic alone, which spell
; the diagonal set out at each instance of a lemma and prove one membership
; twice.  cantor-core-readable proves the same statement with local lemmas
; (pf:have) and lemmas used by matching (pf:by), as Naproche-ZF's proof reads:
; let f be a functional relation from A onto the power set of A; let B be the
; diagonal set; then B is in the power set of A; so f relates some a in A to
; B; a is not in B, and a is in B; contradiction.  Both compile to native
; terms; the readable proof's package carries each pf:by step as the
; eliminations the checker found for it, and both are written as Megalodon
; articles.  The definitions and lemmas below are the curriculum's.
!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))

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
; into introduction and elimination, extensionality, and identity
!(set:define &thy Subq (-> set set prop) (= (Subq $X $Y) (all set (lam x (imp (In x $X) (In x $Y))))))
!(set:define &thy same (-> set set prop)
   (= (same $x $y) (all (-> set prop) (lam Q (imp (Q $x) (Q $y))))))
; sets: a set is a member of the power set of each set that includes it
!(pf:theorem &thy power-intro (all set (lam a (all set (lam b (imp (Subq b a) (In b (Power a)))))))
   (pf:fix a (pf:fix b (pf:assume h
     (pf:imp-elim
       (pf:all-elim (pf:all-elim (pf:all-elim (pf:known powerLaw) a) b) (In b (Power a)))
       (pf:assume f1 (pf:assume g2 (pf:imp-elim g2 h))))))))
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
; pick a b X is a when Empty is a member of X, and b when it is not
!(set:define &thy pick (-> set set set set)
   (= (pick $a $b $X)
     (Eps_set (lam w (and (imp (In Empty $X) (same w $a)) (imp (not (In Empty $X)) (same w $b)))))))
; both a b has a and b among its members: replacement over {Empty, {Empty}}
!(set:define &thy both (-> set set set)
   (= (both $a $b) (Repl (Power (Power Empty)) (lam X (pick $a $b X)))))
; the unordered pair: the members of both a b that are a or b
!(set:define &thy UPair (-> set set set)
   (= (UPair $a $b) (Sep (both $a $b) (lam w (or (same w $a) (same w $b))))))
; the singleton, and Kuratowski's ordered pair
!(set:define &thy Sing (-> set set) (= (Sing $a) (Sep (Power $a) (lam w (same w $a)))))
!(set:define &thy kpair (-> set set set) (= (kpair $a $b) (UPair (Sing $a) (UPair $a $b))))
; ---- functions (Naproche-ZF library: functions): a function from A to B is a
; functional and total relation inside the product; a surjection reaches every
; member of B
!(set:define &thy functional (-> set prop)
   (= (functional $f)
     (all set
       (lam x
         (all set
           (lam y (all set (lam z (imp (In (kpair x y) $f) (imp (In (kpair x z) $f) (same y z)))))))))))
!(set:define &thy onto (-> set set set prop)
   (= (onto $f $A $B)
     (all set (lam y (imp (In y $B) (ex (lam x (and (In x $A) (In (kpair x y) $f)))))))))
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
                             (pf:imp-elim
                               (pf:all-elim
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
                                               (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                                         f))
                                     (pf:assume l3 (pf:assume r4 r4))))
                                 (lam m (In a m)))
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
                       (pf:imp-elim
                         (pf:all-elim
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
                                       (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                                   f))
                               (pf:assume l7 (pf:assume r8 r8))))
                           (lam m (In a m)))
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
                       (pf:imp-elim
                         (pf:all-elim
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
                                       (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                                   f))
                               (pf:assume l13 (pf:assume r14 r14))))
                           (lam m (In a m)))
                         aY))
                     (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                   (pf:imp-elim
                     (pf:all-elim ha
                       (In
                         (kpair a
                           (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                         f))
                     (pf:assume l15 (pf:assume r16 r16))))
                 (pf:imp-elim
                   (pf:all-elim
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
                               (Sep A (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y))))))))
                             f))
                         (pf:assume l17 (pf:assume r18 r18))))
                     (lam m (In a m)))
                   aY)))))))))))))))

; ---- the same, sentence by sentence
; Let B be the diagonal set of f on A: the members x of A in no value of f at x.
!(set:define &thy diagonal (-> set set set)
   (= (diagonal $A $f) (Sep $A (lam x (all set (lam Y (imp (In (kpair x Y) $f) (not (In x Y)))))))))
!(pf:theorem &thy cantor-core-readable
   (all set (lam A (all set (lam f (imp (functional f) (imp (onto f A (Power A)) Falsum))))))
   ; Let f be a functional relation from A onto the power set of A.
   (pf:fix A (pf:fix f (pf:assume hf (pf:assume ho
   ; Then B is in the power set of A.
   (pf:have B-in-power (In (diagonal A f) (Power A)) (pf:by power-intro (pf:by sep-subq))
   ; So f relates some a in A to B.  Consider such an a.
   (pf:by ho B-in-power (pf:fix a (pf:assume ha
     (pf:have a-in-A (In a A) (pf:by ha (pf:assume l (pf:assume r l)))
     (pf:have a-to-B (In (kpair a (diagonal A f)) f) (pf:by ha (pf:assume l (pf:assume r r)))
     ; a is not in B: if it were, it would be in no value of f at a, B among them.
     (pf:have a-out (not (In a (diagonal A f)))
       (pf:assume a-in
         (pf:have outside (all set (lam Y (imp (In (kpair a Y) f) (not (In a Y)))))
           (pf:imp-elim
             (pf:all-elim
               (pf:all-elim (pf:all-elim (pf:known sep-elim-prop) A)
                 (lam x (all set (lam Y (imp (In (kpair x Y) f) (not (In x Y)))))))
               a)
             a-in)
           (pf:by outside a-to-B a-in)))
     ; So a is in B: a value Y of f at a with a in Y is B, f being functional.
     (pf:have a-in-B (In a (diagonal A f))
       (pf:by sep-intro a-in-A
         (pf:fix Y (pf:assume aY-f (pf:assume aY
           (pf:have Y-is-B (same Y (diagonal A f)) (pf:by hf aY-f a-to-B)
             (pf:by a-out (pf:by Y-is-B aY)))))))
     ; Contradiction.
     (pf:by a-out a-in-B))))))))))))))

; The two proofs prove one statement, cite the same lemmas by name and rest on
; the same laws.
!(== (set:known-proposition &thy cantor-core) (set:known-proposition &thy cantor-core-readable))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy cantor-core)
   (Cites $name (map-atom $rows $r (car-atom $r)) (rests-on (pf:rests-on &thy cantor-core))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest)
      (set:native-proof &thy cantor-core-readable)
   (Cites $name (map-atom $rows $r (car-atom $r)) (rests-on (pf:rests-on &thy cantor-core-readable))))
; The readable proof's package carries the proof as it was compiled: each
; pf:by step is the eliminations it stands for, and the local lemmas stay.
; That proof is a proof of the statement in its own right.
(= (mentions $t $s)
   (if (== $t $s) True
       (if (== (get-metatype $t) Expression) (mentions-any $t $s) False)))
(: mentions-any (-> (Data Atom) Atom Bool))
(= (mentions-any $t $s)
   (if (== $t ()) False
       (let $h (car-atom $t)
         (let $rest (cdr-atom $t)
           (if (mentions $h $s) True (mentions-any $rest $s))){$rest}){$h}))
!(let (quote (SetNativeProofV1 $name $prop $proof $term $type $context $rules $rows $digest))
      (lift (set:native-proof &thy cantor-core-readable))
   (let $authored (set:known-proof &thy cantor-core-readable)
     (Written (mentions $authored pf:by) (mentions $proof pf:by) (mentions $proof pf:have)
              (set:proves &thy (quote $proof) (quote $prop)))))
