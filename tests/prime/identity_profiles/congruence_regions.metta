; The native scoped-identity construction: congruence of a function over an
; identity route, built from elimination by an ordinary program, specialized
; to routes between routes (B := id Nat z z), with a region as an explicit
; assumption where the profile admits one.  Run under identity-j and
; identity-scoped and compare.

!(id:policy)
(: Nat (u 0))
(: z Nat)
(: C (u 0))
(: D (u 0))
(: f (-> (id Nat z z) C))
(: g (-> C D))
(: p1 (id Nat z z))
(: p2 (id Nat z z))
(: r (id (id Nat z z) p1 p2))

; congruence f r : id C (f p) (f q), built from elimination:
;   (id:eliminate B p (lam y (lam e (id C (f p) (f y)))) (refl (f p)) q r)

; 1. Reflexivity computes to the reflexivity of the image.
!(type:eq (id:eliminate (id Nat z z) p1 (lam y (lam e (id C (f p1) (f y)))) (refl (f p1)) p1 (refl p1))
          (refl (f p1)))
; 2. An assumed route stays neutral: typed, and distinct from reflexivity.
!(type:of (id:eliminate (id Nat z z) p1 (lam y (lam e (id C (f p1) (f y)))) (refl (f p1)) p2 r))
!(type:eq (id:eliminate (id Nat z z) p1 (lam y (lam e (id C (f p1) (f y)))) (refl (f p1)) p2 r)
          (refl (f p1)))
; 3. Wrong endpoints are refuted.
!(type:check (id:eliminate (id Nat z z) p1 (lam y (lam e (id C (f p1) (f y)))) (refl (f p1)) p2 (refl p1))
             (id C (f p1) (f p2)))
; 4. Nested application: congruence of g over the congruence of f.
!(type:of (id:eliminate C (f p1) (lam y (lam e (id D (g (f p1)) (g y)))) (refl (g (f p1))) (f p2)
            (id:eliminate (id Nat z z) p1 (lam y (lam e (id C (f p1) (f y)))) (refl (f p1)) p2 r)))
; 5. Capture-avoiding substitution of the motive: a constant named y and a
;    motive binding y.
(: Vec (-> Nat (u 0)))
(: y Nat)
(: q (id Nat y z))
(: w (Vec y))
!(type:of (id:eliminate Nat y (lam y (lam e (Vec y))) w z q))
!(type:of (id:eliminate Nat y (lam k (lam e (Vec y))) w z q))
; 6. Leaving the assumption context: the conditional theorem is exported
;    under every profile, with the route equality as its hypothesis.
!(type:check (lam u (id:eliminate (id Nat z z) p1 (lam y (lam e (id C (f p1) (f y)))) (refl (f p1)) p2 u))
             (-> (u : (id (id Nat z z) p1 p2)) (id C (f p1) (f p2))))
; 7. Discharge with the explicit assumption, where the profile admits it.
;    Under identity-j the region is inert, so the assumption's name is
;    undeclared, and the type: layer refutes an application whose head is
;    undeclared in the default space (it abstains under an explicit space).
!(id:region Nat)
!(type:check (id:eliminate (id Nat z z) p1 (lam y (lam e (id C (f p1) (f y)))) (refl (f p1)) p2 (id:assumed-unique@Nat z z p1 p2))
             (id C (f p1) (f p2)))
; 8. A theorem exported through the type: layer is the judgment it was
;    checked as, in the context that judgment used.  exported uses the
;    assumption, exported-refl uses none, and exported-g is built on exported.
;    Withdrawing the assumption leaves every published declaration as it was
;    and turns none into an implication.  A typing judgment that would reuse
;    a theorem whose context is unavailable abstains, directly or through
;    another theorem, and try names what is missing.  The theorem that holds
;    without the assumption is the conditional one of section 6, which a
;    program proves.  Under identity-j the assumption is never declared, so
;    nothing that needs it is published.
!(type:theorem exported (id C (f p1) (f p2))
               (id:eliminate (id Nat z z) p1 (lam y (lam e (id C (f p1) (f y)))) (refl (f p1)) p2 (id:assumed-unique@Nat z z p1 p2)))
!(type:theorem exported-refl (id C (f p1) (f p1)) (refl (f p1)))
!(type:theorem exported-g (id D (g (f p1)) (g (f p2)))
               (id:eliminate C (f p1) (lam y (lam e (id D (g (f p1)) (g y)))) (refl (g (f p1))) (f p2) exported))
!(match &self (: id:assumed-unique@Nat $t) (remove-atom &self (: id:assumed-unique@Nat $t)))
!(match &self (: exported $t) $t)
!(type:of exported)
!(try (type:of exported))
!(type:check exported (id C (f p1) (f p2)))
!(type:of exported-g)
!(type:theorem exported-g2 (id D (g (f p1)) (g (f p2)))
               (id:eliminate C (f p1) (lam y (lam e (id D (g (f p1)) (g y)))) (refl (g (f p1))) (f p2) exported))
!(type:of exported-refl)
; 9. With the assumption published again, the recorded context is available
;    and the theorems are reused as they were published.
!(id:region Nat)
!(type:of exported)
!(type:of exported-g)
