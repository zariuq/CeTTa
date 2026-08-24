; The native regular kernel checks the Curry--Howard core used by proof
; search: assumptions are lexical binders, applying a proof of A -> B to a
; proof of A constructs a proof of B, and dependent results retain their
; witnesses.  Search and declaration indexing remain separate concerns.

; A named binder and its positional spelling elaborate to the same intrinsic
; identity term.
!(type:check (lam x x) (-> u0 u0))
!(type:check (lam x (idx 0)) (-> u0 u0))

; The codomain depends on x: the result is a proof that x equals itself.
!(type:check
     (lam x (refl x))
     (-> (x : u0) (id u0 x x)))

; A dependent pair packages a witness together with equality evidence about
; that exact witness.
!(type:check
     (lam x (pair x (refl x)))
     (-> (x : u0)
         (sigma (y : u0) (id u0 x y))))

; Modus Ponens is ordinary proof application.  This is the native typed core
; beneath declaration-backed ATP and PLN chaining.
!(type:check
     (lam (p mp) (mp p))
     (-> (p : u0) (mp : (-> u0 u0)) u0))

; Higher-order proof composition uses the same multibinder elaboration.
!(type:check
     (lam (f g x) (g (f x)))
     (-> (f g : (-> u0 u0)) (x : u0) u0))

; Negative control: applying evidence at the wrong domain is refuted inside
; the class with checked obstruction.
!(type:check
     (lam (p mp) (mp mp))
     (-> (p : u0) (mp : (-> u0 u0)) u0))

; Tower-level negative control: refl u0 synthesizes (id u1 u0 u0), which is
; formed but differs from the claimed (id u0 x x).  The native tower therefore
; returns a checked refutation rather than the old sealed-fragment abstention.
!(type:check
     (lam x (refl u0))
     (-> (x : u0) (id u0 x x)))

; The first search-facing rung keeps candidate enumeration in ordinary
; relational MeTTa while giving native Prime the final word on every proof.
; These records are data for search, not trusted type declarations.
(dtt-proof IdentityU0
  (lam x x)
  (-> u0 u0))
(dtt-proof IdentityOnFunction
  (lam p p)
  (-> (-> u0 u0) (-> u0 u0)))
(dtt-proof BadClaim
  (lam x x)
  u0)

; Enumerate declarations, then validate each record through the native
; regular kernel.  The deliberately false claim remains visible and Refuted.
!(match &self
   (dtt-proof $name $proof $type)
   (let $accepted (type:check $proof $type)
     ((Candidate $name) $accepted)))
