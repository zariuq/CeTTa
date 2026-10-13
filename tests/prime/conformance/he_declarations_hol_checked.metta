; HE-style declarations, answered through the HOL contract.  Inside a grounded
; fragment the hosted HOL Light checker decides each judgment: an answer it
; accepts is established, and one it refuses is refuted.  Outside every
; fragment a judgment stays unevaluated; it is never refuted.

(: Nat Type)
(: Bool Type)
(: A Type)
(: B Type)
(: List Type)
(: Z Nat)
(: Truth Bool)
(: a A)
(: f (-> A B))
(: g (-> A A))
(: twice (-> (-> A A) A A))

; Static arrows: declared constants and saturated applications.
!(Arrows
   (type:of (f a))
   (type:check (f a) B)
   (type:check (f a) A)
   (type:check (f Z) B)
   (type:of (twice g a)))

; First-order schemes: a marked open declaration is a polymorphic constant,
; read at the instance its occurrence takes.
(: nil (List $t))
(: cons (-> $t (List $t) (List $t)))
(type-scheme nil)
(type-scheme cons)
!(Schemes
   (type:of (cons Z nil))
   (type:check (cons Z nil) (List Nat))
   (type:check nil (List A))
   (type:check (cons Z nil) (List A))
   (type:check (cons Z Z) (List Nat)))

; A rigid variable is never solved: an unmarked open declaration answers
; neither way.
(: hold (List $r))
!(Rigid
   (type:check hold (List A))
   (type:check hold Nat))

; Tuples are products: an expression whose head has a non-arrow type has the
; tuple of its elements' types, in order.
!(Tuples
   (type:of (Truth Z))
   (type:check (Truth Z) (Bool Nat))
   (type:check (Truth Z) (Nat Bool))
   (type:formed (Bool Nat)))

; Meta-types classify an atom by its shape.
!(MetaTypes
   (type:analyze Truth Symbol)
   (type:analyze (f a) Expression)
   (type:analyze 7 Grounded)
   (type:analyze (f a) Symbol)
   (type:analyze Truth Expression))

; %Undefined% is the unknown type of gradual typing: consistency, never a
; refutation.
(: vague %Undefined%)
(: partial (List %Undefined%))
!(Gradual
   (type:analyze vague Nat)
   (type:analyze (cons Z nil) (List %Undefined%))
   (type:analyze partial (List Nat))
   (type:analyze partial Nat)
   (type:check vague Nat))

; Overloads: every declaration is its own constant, so a symbol declared
; twice has both types, and a judgment has one answer per derivation.
(: dup A)
(: dup B)
(: pick (-> A Nat))
(: pick (-> B Nat))
!(Overloads
   (type:check dup A)
   (type:check dup B)
   (type:check dup Nat)
   (type:check (pick dup) Nat)
   (type:check (pick dup) A))

; An arrow is a type of its own arity: an application of another arity has
; no type, and an arrow returning an arrow takes one argument at a time.
(: k (-> A (-> A B)))
!(Arity
   (type:check (f a a) B)
   (type:check (f) B)
   (type:check ((k a) a) B)
   (type:check (k a a) B)
   (type:check (k a) (-> A B)))

; A binder `(: $x A)` names its argument, the `he-prime` extension: what
; follows it sees the argument itself.  The hosted LF kernel decides it.
(: Fam (-> A Type))
(: dep (-> (: $x A) (Fam $x)))
(: a2 A)
!(Dependent
   (type:of (dep a))
   (type:check (dep a) (Fam a))
   (type:check (dep a) (Fam a2))
   (type:check (dep Z) (Fam a)))

; A declaration whose type applies a declared type family outside its domain
; declares nothing.
(: badFam (Fam Z))
!(IllFormed
   (type:of badFam)
   (type:check badFam (Fam a)))

; Outside every fragment: an undeclared head.
!(Outside
   (type:of (mystery a)))

; A telescope component whose variable occurs nowhere later binds nothing:
; the arrow is the simple one, and the checker decides it.
(: vacuous (-> (: $x A) B))
!(Binders
   (type:check (vacuous a) B)
   (type:check (vacuous Z) B))

; A base type declared HE-style forms; so do its arrows and its products.
!(Formation
   (type:formed Nat)
   (type:formed (-> Nat Nat))
   (type:formed (Nat Bool))
   (type:formed (-> Nat Mystery)))

; An arrow inside a product is a type of its own arity, so `(-> A (-> A B))`
; and `(-> A A B)` are different types.  A scheme's variable may stand for an
; arrow.  `(T)` is the product of `T` and the unit type.
(: pairs (List (-> A B)))
(: curried (List (-> A (-> A B))))
(: single (Nat))
!(Faithful
   (type:check pairs (List (-> A B)))
   (type:check pairs (List (-> B A)))
   (type:check curried (List (-> A A B)))
   (type:check curried (List (-> A (-> A B))))
   (type:check nil (List (-> A B)))
   (type:check single (Nat))
   (type:check single Nat))

; get-type: the checked types, one per derivation, no type when the checker
; refuses every way of typing the atom, and the gradual type of an
; application whose arguments are consistent with its head's parameters.
!(get-type (f a))
!(collapse (get-type (cons Z Z)))
!(get-type dup)
!(get-type (pick dup))
!(get-type ((k a) a))
!(collapse (get-type (k a a)))
!(get-type (f mystery))
!(get-type 7)

; The gradual rule through a first-order scheme: an argument that nothing
; types constrains no variable; a typed argument must match its parameter.
(: Indexed (-> $t Number Type))
!(get-type (Indexed mystery 3))
!(get-type (Indexed mystery Z))

; Malformed telescopes do not acquire a verdict through the dependent route.
(: repeated-binder (-> (: $x A) (: $x A) (Fam $x)))
(: forward-binder (-> (: $x (Fam $y)) (: $y A) (Fam $y)))
(: self-binder (-> (: $x (Fam $x)) (Fam $x)))
!(BinderScopes
   (type:check (repeated-binder a a) (Fam a))
   (type:check (forward-binder a a) (Fam a))
   (type:check (self-binder a) (Fam a)))

; An instantiated arrow must still belong to the contract grammar.
(: scoped-list (List Atom))
(: scoped-f (-> (List $t) $t Nat))
(type-scheme scoped-f)
(: scoped-z $s)
(type-scheme scoped-z)
!(InstanceAdmission (type:check (scoped-f scoped-list scoped-z) Nat))
