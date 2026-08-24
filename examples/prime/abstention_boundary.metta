; Coverage-boundary regression: inert abstention is never negative pruning
; license.  The public boolean contract for the native regular kernel:
;
;   True   == judgment inside the fragment class, checked derivation;
;   False  == judgment inside the class, checked obstruction (the inferred
;             evidence is the counterexample);
;   inert  == judgment outside the class: the fragment declines.  A failed
;             scope/formation search is not evidence against the Prime-level
;             judgment, so out-of-class positions must never read False.
;
; Rows 1-4 pin the decided region: every False below is a checked
; obstruction with both positions formed inside the class.
; Row 5 pins the remaining abstention region: its variables-as-types reading
; awaits declaration-level polymorphic instantiation.  Rows 6-8 pin the first
; closed-tower expansion: type-level equality and large formation are now
; decided, and synthesis of a universe returns its explicit successor.

; 1. in-class inhabitance (positive control).
!(type:check (lam (p mp) (mp p)) (-> (p : u0) (mp : (-> u0 u0)) u0))

; 2. in-class refutation: applying a function to itself — checked
; type-mismatch, stays False.
!(type:check (lam (p mp) (mp mp)) (-> (p : u0) (mp : (-> u0 u0)) u0))

; 3. in-class refutation: lambda against a formed non-function type.
!(type:check (lam x x) u0)

; 4. in-class refutation: equality proof for x = x claimed as x = y —
; checked distinct-normal-forms.
!(type:check (lam (x y) (refl x)) (-> (x y : u0) (id u0 x y)))

; 5. out-of-class: Nil's polymorphic Modus Ponens with variables used as
; types.  Live in the tower (p : Type-quantified); abstains here.
!(type:check
     (lam (p q h x) (h x))
     (-> (p q : u0) (h : (-> p q)) (x : p) q))

; 6. tower refutation: refl of a type has (id u1 u0 u0), which cannot inhabit
; the claimed (id u0 x x).
!(type:check (lam x (refl u0)) (-> (x : u0) (id u0 x x)))

; 7. tower refutation: the large Sigma is formed, so a lambda checked against
; it is a witnessed non-function mismatch.
!(type:check (lam x x) (sigma (T : u1) u0))

; 8. tower synthesis: asking the type of the embedded upper marker returns
; the next explicit level.
!(type:of u1)
