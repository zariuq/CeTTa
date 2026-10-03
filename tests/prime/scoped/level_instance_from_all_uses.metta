; The level instance of a declaration is the least assignment of its level
; variables at which the whole judgment holds.  Every use of a variable
; counts, in whatever order the uses are written: an argument that is a type
; bounds the variable from below, a domain of a function type fixes it, and
; an expected type is a use like any other.  What a variable holds while the
; uses are read is a lower bound on it, never its value: a use that asks for
; more raises it, and a judgment is refuted only where no instance has it.
; (Mettapedia, TypeTheory/UniverseLevel/LeastInstance.lean.)

!(bind! &s (new-space))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
!(add-atom &s (: Small (u 3)))
!(add-atom &s (: T4 (u 4)))
!(add-atom &s (: T5 (u 5)))
!(add-atom &s (: T7 (u 7)))
!(add-atom &s (: T9 (u 9)))
!(add-atom &s (: f5 (-> (u 5) num)))
!(add-atom &s (: f7 (-> (u 7) num)))
!(add-atom &s (: fo (-> (u omega) num)))

; Three uses of one variable.  X and Y bound it from below; F fixes it,
; since a function type is usable only at function types of the same domain.
; With Small, f5 and T5 the uses are 3 <= l, l = 5 and 5 <= l: the least
; instance is 5, in both orders.
!(add-atom &s (: k2 (-> (X : (u $l)) (-> (F : (-> (u $l) num)) (-> (Y : (u $l)) num)))))
!(type:of &s (k2 Small f5 T5))
!(type:of &s (k2 T5 f5 Small))
!(type:check &s (k2 Small f5 T5) num)
!(try (type:of &s (k2 Small f5 T5)))
; Negative: 7 <= l and l = 5 hold at no instance, in every order.
!(type:of &s (k2 Small f5 T7))
!(try (type:of &s (k2 Small f5 T7)))
!(try (type:of &s (k2 T7 f5 Small)))
!(try (type:of &s (k2 T7 f5 T5)))
!(type:check &s (k2 T7 f5 Small) num)

; The same with long numerals.  Vast is a type of the universe at 10^26 - 1
; and fv takes the universe at 10^26: with Vast, fv and Small the uses are
; 10^26 - 1 <= l, l = 10^26 and 3 <= l, and the instance is 10^26.
!(add-atom &s (: Vast (u 99999999999999999999999999)))
!(add-atom &s (: fv (-> (u 100000000000000000000000000) num)))
!(type:of &s (k2 Vast fv Small))
!(try (type:of &s (k2 Small fv Vast)))
; Negative: with f5 the uses are 10^26 - 1 <= l and l = 5, which hold at no
; instance.
!(type:of &s (k2 Vast f5 Small))
!(try (type:of &s (k2 Vast f5 Small)))

; The expected type is a use.  (k Small) is typed at every l >= 3.  Without
; an expected type its type is the one at the least instance.  At the
; expected type (-> (u 5) num) the instance is 5, and the judgment is the
; one of k5, the same declaration written at 5.
!(add-atom &s (: k (-> (X : (u $l)) (-> (u $l) num))))
!(add-atom &s (: k5 (-> (X : (u 5)) (-> (u 5) num))))
!(type:of &s (k Small))
!(type:check &s (k Small) (-> (u 3) num))
!(type:check &s (k Small) (-> (u 5) num))
!(type:check &s (k5 Small) (-> (u 5) num))
; Negative: 3 <= l and l = 2 hold at no instance.
!(type:check &s (k Small) (-> (u 2) num))
!(try (type:check &s (k Small) (-> (u 2) num)))

; An equation under a successor.  F fixes l + 1; with f5 that is l = 4.
!(add-atom &s (: ks (-> (X : (u $l)) (-> (F : (-> (u (+ $l 1)) num)) num))))
!(type:of &s (ks Small f5))
!(type:of &s (ks T4 f5))
; Negative: 5 <= l and l + 1 = 5 hold at no instance.
!(type:of &s (ks T5 f5))
!(try (type:of &s (ks T5 f5)))
; Negative: omega is the successor of no level, so l + 1 = omega holds at
; no instance.
!(try (type:of &s (ks num fo)))

; A bound that arrives through another declaration.  (mkf Small) has the
; type (-> (u a) num) for a >= 3, and use fixes its variable b at a, bounds
; it by 5 and fixes it at 7.  When a and b are first made equal they are
; both 3; the later uses raise b to 7, and a follows: the instance is
; a = b = 7.
!(add-atom &s (: mkf (-> (X : (u $a)) (-> (u $a) num))))
!(add-atom &s (: use (-> (G : (-> (u $b) num)) (-> (Y : (u $b)) (-> (H : (-> (u $b) num)) num)))))
!(type:of &s (use (mkf Small) T5 f7))
!(try (type:of &s (use (mkf Small) T5 f7)))
; Negative: 9 <= a, a = b and b = 7 hold at no instance.
!(type:of &s (use (mkf T9) T5 f7))
!(try (type:of &s (use (mkf T9) T5 f7)))

; The question whether two terms are equal is a use as well.  It asks for an
; instance at which they are equal at a common type; their own types need a
; common upper bound and need not be equal.  (c f3) is typed at l = 3 only,
; in (u 4).  Seen through an identity on (u 6) it is the same term in a
; larger universe, and the two are equal there.  Nothing raises l to bring
; (u 4) up to (u 6): at l = 5 the argument f3 would no longer fit.
!(add-atom &s (: f3 (-> (u 3) num)))
!(add-atom &s (: c (-> (F : (-> (u $l) num)) (u (+ $l 1)))))
!(add-atom &s (: c3 (-> (F : (-> (u 3) num)) (u 4))))
!(type:eq &s (c f3) ((lam (Y : (u 6)) Y) (c f3)))
!(type:eq &s ((lam (Y : (u 6)) Y) (c f3)) (c f3))
!(try (type:eq &s (c f3) ((lam (Y : (u 6)) Y) (c f3))))
; The same question with the instance written out.
!(type:eq &s (c3 f3) ((lam (Y : (u 6)) Y) (c3 f3)))
; A family into a universe, seen as a family into a larger one.
!(add-atom &s (: cf (-> (F : (-> (u $l) num)) (-> num (u (+ $l 1))))))
!(type:eq &s (cf f3) ((lam (Y : (-> num (u 6))) Y) (cf f3)))
!(type:eq &s (cf f3 zero) ((lam (Y : (-> num (u 6))) (Y zero)) (cf f3)))
; Each occurrence of a declaration has its own instance: k is used at 3 and
; at 5 in one term.
!(add-atom &s (: two (-> (-> (u 3) num) (-> (-> (u 5) num) num))))
!(type:of &s (two (k Small) (k T5)))
; Negative: different constants are equal at no instance.
!(add-atom &s (: d (-> (F : (-> (u $l) num)) (u (+ $l 1)))))
!(type:eq &s (c f3) (d f3))
!(try (type:eq &s (c f3) (d f3)))
; Negative: equal heads ask for one instance, and the arguments fix it at 3
; and at 5.
!(try (type:eq &s (c f3) (c f5)))
; Negative: a function type and a universe have no common upper bound.
!(try (type:eq &s c (c f3)))
; Negative: seen in a smaller universe the term is not typed, with the
; instance found and with the instance written out.
!(try (type:eq &s (c f3) ((lam (Y : (u 2)) Y) (c f3))))
!(try (type:eq &s (c3 f3) ((lam (Y : (u 2)) Y) (c3 f3))))
