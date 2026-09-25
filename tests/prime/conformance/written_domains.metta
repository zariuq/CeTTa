; A written lambda domain is a contract: checking compares it with the domain
; of the expected function type, and evaluation runs the same reduction with
; or without it.
(: Nat (u 0))
(: Bool (u 0))
(: z Nat)
(: s (-> Nat Nat))
(: tt Bool)

; Identities with and without written domains.
!(type:check (lam x x) (-> Nat Nat))
!(type:check (lam (x : Nat) x) (-> Nat Nat))
!(type:check (lam (x : Bool) x) (-> Bool Bool))
!(type:of (lam (x : Nat) x))
!(type:of (lam (x : Nat) (s x)))

; Grouped and mixed telescopes, including a dependent one.
!(type:check (lam (x y : Nat) y) (-> Nat Nat Nat))
!(type:check (lam (x (y : Nat)) y) (-> Nat Nat Nat))
!(type:check (lam ((A : (u 0)) (x : A)) x) (-> (A : (u 0)) (x : A) A))
!(type:check (lam (A (x : A)) x) (-> (A : (u 0)) (x : A) A))

; A written domain that differs from the expected domain is refuted when the
; difference is decidable: another type former, or another universe.
!(type:check (lam (x : (-> Nat Nat)) x) (-> Nat Nat))
!(type:check (lam (x : (u 0)) x) (-> (u 1) (u 1)))
!(type:check (lam ((A : (u 0)) (x : (u 0))) x) (-> (A : (u 0)) (x : A) A))
!(type:check (lam ((A : (u 1)) (x : A)) x) (-> (A : (u 0)) (x : A) A))
; Two declared types are not known to be distinct, so this stays undecided.
!(type:check (lam (x : Bool) x) (-> Nat Nat))

; Evaluation is the same with and without written domains.
!((lam (x : Nat) (s x)) z)
!((lam x (s x)) z)
!((lam (x y : Nat) (s y)) z (s z))
!(((lam (x y : Nat) (s y)) z) (s z))
!((lam ((x : Nat) (y : Nat)) (s x)) z (s z))
!((lam (x y) (pair y x)) z tt)

; Substitution respects the binders of a telescope: an inner binder hides the
; outer one, a binder that would capture a free name is renamed, and the
; written type of a group is read outside the group.
!((lam x (lam (x : Nat) x)) z)
!(((lam y (lam (x : Nat) (pair x y))) x) z)
!(((lam x (lam (x y : (T x)) y)) q) a)

; Quotation observes authored syntax: written domains and redexes stay.
!(== (quote (lam (x : Nat) x)) (quote (lam x x)))
!(== (quote ((lam x x) z)) (quote z))
!(quote ((lam (x : Nat) (s x)) z))
