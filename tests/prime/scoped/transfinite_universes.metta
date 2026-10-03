; Universe levels beyond the finite ones.  A closed level is an ordinal
; notation below epsilon-zero in Cantor normal form: 0, 1, 2, ..., omega,
; (+ omega 1), ..., (* omega 2), ..., (^ omega 2), ..., (^ omega omega), ...
; The universe (u omega) lies above every finite universe.  Each comment
; names the theorem of the Lean development (TypeTheory/UniverseLevel) that
; licenses the verdict.

; A universe is a term of the universe one level up, and of no universe at
; or below its own level: (u a) is a term of (u b) exactly when a + 1 <= b
; (Level.lt_succ, Level.succ_le_of_lt, LevelNF.normalize_le_iff).
!(type:of (u omega))
!(type:check (u omega) (u (+ omega 1)))
!(type:check (u omega) (u omega))
!(type:check (u omega) (u 7))

; Every finite universe is a term of (u omega) (Level.ofNat_lt_omega).
!(type:check (u 0) (u omega))
!(type:check (u 7) (u omega))
; So is the universe at a numeral of any length, and (u omega) is a term of
; none of them.
!(type:check (u 99999999999999999999999999) (u omega))
!(type:check (u omega) (u 99999999999999999999999999))

; The order 7 < omega < omega + 1 < omega * 2 < omega^2 < omega^omega, read
; through universe membership, upward and then downward.
!(type:check (u 7) (u omega))
!(type:check (u omega) (u (+ omega 1)))
!(type:check (u (+ omega 1)) (u (* omega 2)))
!(type:check (u (* omega 2)) (u (^ omega 2)))
!(type:check (u (^ omega 2)) (u (^ omega omega)))
!(type:check (u omega) (u 7))
!(type:check (u (+ omega 1)) (u omega))
!(type:check (u (* omega 2)) (u (+ omega 1)))
!(type:check (u (^ omega 2)) (u (* omega 2)))
!(type:check (u (^ omega omega)) (u (^ omega 2)))
; Distinct notations are distinct universes, and a notation is itself.
!(type:eq (u omega) (u omega))
!(type:eq (u omega) (u (+ omega 1)))
!(type:eq (u (* omega 2)) (u (^ omega 2)))

; Cumulativity: a type of a lower universe is a type of every higher one.
!(bind! &s (new-space))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
!(add-atom &s (: Small (u 3)))
!(add-atom &s (: Big (u omega)))
!(add-atom &s (: big Big))
!(type:of &s Big)
!(type:check &s Small (u 3))
!(type:check &s Small (u omega))
!(type:check &s Big (u omega))
!(type:check &s Big (u (* omega 2)))
!(type:check &s num (u omega))
; Not downward: a type of (u omega) is not a type of a finite universe.
!(type:check &s Big (u 7))

; A function type lies in the maximum of the universes of its domain and
; codomain.
!(type:of &s (-> Big num))
!(type:of &s (-> Small Big))
!(type:of &s (-> (X : (u omega)) X))
!(type:of &s (-> (X : (u 3)) (u omega)))
!(type:check &s (-> Big num) (u omega))
!(type:check &s (-> Big num) (u 3))
!(type:check &s (lam (x : Big) x) (-> Big Big))

; A declaration over a universe level is used at omega, and above it.
!(add-atom &s (: poly-id (-> (X : (u $l)) (-> X X))))
!(type:of &s (poly-id Big))
!(type:of &s (poly-id Big big))
!(type:check &s (poly-id Big big) Big)
!(type:of &s (poly-id (u omega)))
!(type:check &s (poly-id (u omega) Big) (u omega))
!(type:of &s (poly-id (u 7)))
; A declared level raised by a successor: at omega it is omega + 1.
!(add-atom &s (: next-universe (-> (X : (u $l)) (u (+ $l 1)))))
!(type:of &s (next-universe Small))
!(type:of &s (next-universe Big))
!(type:of &s (next-universe (u omega)))
