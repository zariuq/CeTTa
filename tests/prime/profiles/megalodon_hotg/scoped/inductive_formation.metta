(set:profile megalodon-hotg)

; Datatype admission needs formed field types, not merely type-shaped names.
; Undeclared or unsupported type formation abstains; a checked universe
; violation is refuted. Neither publishes any part of the proposed datatype.
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(let $r (try (set:inductive &thy GhostTree (u 0)
                (: GhostNode (-> MissingType GhostTree))))
   (car-atom $r))

; A set is a type of the sort of all sets, which lies above (u 0): a field
; whose type is the set Empty is a universe violation in a datatype at (u 0).
!(let $r (try (set:inductive &thy ValueTree (u 0)
                (: ValueNode (-> Empty ValueTree))))
   (car-atom $r))
!(let $r (try (set:inductive &thy BadLevel (u -1)
                (: BadLevelCtor BadLevel)))
   (car-atom $r))

; A type in U1 cannot become a field of a freely generated datatype in U0.
!(add-atom &thy (: Large (u 1)))
!(let $r (try (set:inductive &thy SmallTree (u 0)
                (: SmallNode (-> Large SmallTree))))
   (car-atom $r))

; A plain unknown admission also leaves no partial declaration or principle.
!(set:inductive &thy GhostTree (u 0)
   (: GhostNode (-> MissingType GhostTree)))
!(== (collapse (match &thy (: GhostTree $type) $type){$type}) ())
!(== (collapse (match &thy (: GhostNode $type) $type){$type}) ())
!(== (collapse (match &thy (: GhostTree-rec $type) $type){$type}) ())
!(== (collapse (match &thy
        (set:known GhostTree-ind $p $o $f $d $r $s) $p){$p $o $f $d $r $s}) ())

; After its missing type is supplied, the same declaration earns admission.
; Its dependent recursor computes on the constructor in the native kernel.
!(add-atom &thy (: MissingType (u 0)))
!(add-atom &thy (: element MissingType))
!(set:inductive &thy GhostTree (u 0)
   (: GhostNode (-> MissingType GhostTree)))
!(type:check &thy
   (GhostTree-rec (lam t MissingType) (lam m m) (GhostNode element))
   MissingType)
!(type:eq &thy
   (GhostTree-rec (lam t MissingType) (lam m m) (GhostNode element))
   element)
!(let $r (try (type:check &thy
                (GhostTree-rec (lam t MissingType) (lam m Empty) (GhostNode element))
                MissingType))
   (car-atom $r))

; Higher-order fields remain legal when formed in the declared universe.
!(let $r (try (set:inductive &thy FunctionTree (u 0)
                (: FunctionNode (-> (-> MissingType MissingType) FunctionTree))))
   (car-atom $r))

; A sufficiently large datatype universe admits the previously large field.
!(let $r (try (set:inductive &thy LargeTree (u 1)
                (: LargeNode (-> Large LargeTree))))
   (car-atom $r))

; Generator-private binder names are not reserved declaration names.
!(set:inductive &thy P (u 0) (: MkP P))
!(type:check &thy (P-rec (lam p MissingType) element MkP) MissingType)
!(type:eq &thy (P-rec (lam p MissingType) element MkP) element)
!(set:inductive &thy NameTree (u 0) (: v1 (-> MissingType NameTree)))
!(type:eq &thy
   (NameTree-rec (lam tree MissingType) (lam m m) (v1 element))
   element)

; A constructor cannot reuse the generated eliminator's declaration name.
!(let $r (try (set:inductive &thy ConflictTree (u 0)
                (: ConflictTree-rec ConflictTree)))
   (car-atom $r))

; Field storage follows the telescope's size, not a fixed eight-slot array.
!(let $r (try (set:inductive &thy WideTree (u 0)
                (: WideNode (-> MissingType MissingType MissingType
                                MissingType MissingType MissingType
                                MissingType MissingType MissingType WideTree))))
   (car-atom $r))

; A successful `try` checks but does not publish; admit it before execution.
!(set:inductive &thy WideTree (u 0)
   (: WideNode (-> MissingType MissingType MissingType
                   MissingType MissingType MissingType
                   MissingType MissingType MissingType WideTree)))
!(type:eq &thy
   (WideTree-rec (lam wt MissingType)
     (lam a (lam b (lam c (lam d (lam e (lam f (lam g (lam h (lam i a)))))))))
     (WideNode element element element element element element element element element))
   element)

; Freshness also avoids an occupied generated suffix and field-type names.
!(bind! &names (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile megalodon-hotg)) $profile-space)))
!(add-atom &names (: P (u 0)))
!(add-atom &names (: P~1 (u 0)))
!(add-atom &names (: a P))
!(add-atom &names (: b P~1))
!(set:inductive &names HygieneTree (u 0) (: v1 (-> P P~1 HygieneTree)))
; The stored recursor type, read back, is the recursor's type.  Its motive
; ranges over a universe at a level variable; each instance is checked.
!(let $ty (match &names (: HygieneTree-rec $ty) $ty){$ty}
   (let (-> (: $motive (-> $data (u $level))) $methods) $ty
     (type:check &names HygieneTree-rec
       (-> (: $motive (-> $data (u 0))) $methods)))){$ty}
!(let $ty (match &names (: HygieneTree-rec $ty) $ty){$ty}
   (let (-> (: $motive (-> $data (u $level))) $methods) $ty
     (type:check &names HygieneTree-rec
       (-> (: $motive (-> $data (u 1))) $methods)))){$ty}
!(type:eq &names
   (HygieneTree-rec (lam p P) (lam aa (lam bb aa)) (v1 a b)) a)
