(hol:profile megalodon-hotg)

; Within the HOTG profile, its sets enclose the tower of type universes, which lies
; inside them.  `set`, the type of all sets, is the sort above every universe
; (u l) an author writes, and `class` is its sort.  No typing rule is added:
; every universe is a member of `set` by cumulativity, a type of a universe
; is usable at `set`, and `set` is a member of no universe.  A set is a type.

; The two names are sorts in the type language and in the set language, and
; they print as written.  Their sorts follow: the sort of `class` is the
; next one, (class 2).
!(type:formed set)
!(type:formed class)
!(type:of set)
!(type:of class)
!(hol:of set)
!(hol:formed (-> set prop))

; Every universe at a written level is a member of `set`, at any level.
!(type:check (u 0) set)
!(type:check (u omega) set)
!(type:check (u (^ omega omega)) set)
!(hol:check (u 0) set)
!(try (type:check (u 0) set))
; `set` is a member of `class` and of no universe; `class` is not a member of
; `set`, and neither is a member of itself.
!(type:check set class)
!(try (type:check set (u 0)))
!(type:check set (u omega))
!(hol:check set (u 0))
!(type:check class set)
!(type:check set set)
!(type:check class class)

; A type of the least universe is a set, through cumulativity: a datatype
; declared at (u 0), and a function type between such types.
!(hol:inductive &self num (u 0) (: zero num) (: succ (-> num num)))
!(type:check num set)
!(hol:check &self num set)
!(type:check (-> num num) set)
; A function type into `set`, or over it, is no set: its sort is `class`.
!(type:check (-> set (u 0)) set)
!(type:check (-> set (u 0)) class)
!(type:check (-> num set) set)
!(hol:check (-> set prop) class)
!(hol:check (-> set prop) set)

; A set is a type: a variable may range over a set, and over all sets.
!(hol:of Empty)
!(type:check (lam (A : set) (lam (x : A) x)) (-> (A : set) (-> A A)))
!(type:check (-> (A : set) (-> A A)) class)
!(type:check (-> (A : set) (-> A A)) set)

; A datatype may be declared at `set`: it is then a set, and not a type of
; the least universe.
!(hol:inductive &self bag set (: empty-bag bag) (: put (-> num bag bag)))
!(hol:check &self bag set)
!(try (hol:check &self bag (u 0)))
!(type:check (lam (b : bag) b) (-> bag bag))

; The signature declares no type of `set`; the quantifier and the equality
; range over every type of `class`, with no level variable.
!(hol:signature)
; So `all` quantifies over all sets, over the predicates on them and over
; the propositions, and the rule of a definition names `all` with no
; universe argument.  `class` itself is no type of `class`.
!(hol:check (all set (lam x (In x x))) prop)
!(hol:check (all (-> set prop) (lam P (P Empty))) prop)
!(hol:check (all prop (lam p p)) prop)
!(hol:check &self (all bag (lam b (eq bag b b))) prop)
!(hol:check (all class (lam C (In Empty Empty))) prop)
!(try (hol:check (all class (lam C (In Empty Empty))) prop))
!(hol:of (all set))

; A proof about all sets, used at an actual set, is checked by the proof
; rules: Empty is not a member of itself, from the law emptyLaw.
!(hol:proves (pf:imp-intro (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty)
                                         (pf:hyp 0)))
             (imp (In Empty Empty) Falsum))
!(hol:proves (pf:imp-intro (pf:hyp 0)) (imp (In Empty Empty) Falsum))
; The native proof compiler decodes a quantifier only over a carrier of the
; least universe, and `set` is above every universe: a quantifier over all
; sets is compiled to the rule constants `allI@set` and `allE@set`, and a
; theorem over the propositions by abstraction and application.  Each has a
; native package.
!(hol:theorem empty-not-self (imp (In Empty Empty) Falsum)
   (pf:imp-intro (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty) (pf:hyp 0))))
!(let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (hol:native-proof empty-not-self)
   (let (Lam $hypothesis (App (App (App (App (DeclConst $rule) $predicate) $law) $witness)
                              $premise))
        $term
     (NativeProof $name $rule $witness (size-atom $assumptions))))
!(hol:theorem self-implies (all set (lam x (imp (In x x) (In x x))))
   (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (hol:native-proof self-implies)
   (let (App (App (DeclConst $rule) $predicate) (Lam $carrier $body)) $term
     (NativeProof $name $rule (size-atom $assumptions))))
!(hol:theorem prop-implies (all prop (lam p (imp p p)))
   (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (hol:native-proof prop-implies)
   (let (Lam (DeclConst prop) $body) $term
     (NativeProof $name abstraction (size-atom $assumptions))))

; A judgment whose level arithmetic runs out is incomplete, never refuted.
!(try (hol:check (u (^ (+ omega 1) 1000000000)) set))
!(type:check (u (^ (+ omega 1) 1000000000)) set 100000)
!(type:check (u (^ (+ omega 1) 50)) set 100000)
