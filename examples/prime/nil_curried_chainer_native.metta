; Nil's curried backward chainer meeting the native Prime kernel.
; HE source: examples/bc_curried.metta (curried DTL, itself a port of
; hyperon/chaining/dtl/backward/curried.metta).
;
; The HE chainer synthesizes types BY UNIFICATION: declared $-schemas such as
;   (: ModusPonens (-> (-> $p $q) (-> $p $q)))
; are instantiated afresh at every match row.  Here the same relational
; search stays in ordinary MeTTa, acceptance of a proof belongs to the
; native kernel, and $-schemas are translated toward explicit Pi.  Tiers:
;
;   A. rules as native proof terms                 -- decided today;
;   B. dependent inhabitation + the rung targets   -- partly decided today,
;      with the polymorphism rung pinned as inert-abstaining under the
;      coverage ruling, the annotated-head rung likewise abstaining, and
;      the declaration rung still pinned;
;   C. Nil's chainer, verbatim search shape, every found proof handed to
;      native admission -- runs today, upgrades with the declaration rung.
;
; The chainer carries no (: syn ...) declaration on purpose: typing the
; chainer itself is the declaration rung's job, not a $-schema's.

; ---------------------------------------------------------------------
; Tier A: the rules at monomorphic instances (inside today's fragment).
; ---------------------------------------------------------------------

; Modus Ponens is proof application.
!(type:check
     (lam (h x) (h x))
     (-> (h : (-> u0 u0)) (x : u0) u0))

; Deduction (syllogism) is composition.
!(type:check
     (lam (g f x) (g (f x)))
     (-> (g f : (-> u0 u0)) (x : u0) u0))

; Negative control: applying an argument to itself is genuinely ill-typed.
!(type:check
     (lam (g x) (x g))
     (-> (g : (-> u0 u0)) (x : u0) u0))

; ---------------------------------------------------------------------
; Tier B: dependent content (decided today) and the pinned rung targets.
; ---------------------------------------------------------------------

; Dependent inhabitation is real now: self-identification and witness
; packaging check natively.
!(type:check (lam x (refl x))
             (-> (x : u0) (id u0 x x)))
!(type:check (lam (f x) (pair x (f x)))
             (-> (-> (x : u0) (id u0 x x))
                 (-> (x : u0) (sigma (y : u0) (id u0 x y)))))

; POLYMORPHISM-RUNG TARGET.  Nil's schema (: ModusPonens (-> (-> $p $q)
; (-> $p $q))) translates to Pi over u0 with bound VARIABLES USED AS TYPES.
; Variables-as-types lie outside the declaration-free fragment, and the
; coverage ruling is settled: out-of-fragment judgments ABSTAIN (inert),
; never Refuted.  Refuted requires a checked obstruction inside the class.
!(type:check
     (lam (p q h x) (h x))
     (-> (p q : u0) (h : (-> p q)) (x : p) q))

; ANNOTATED-HEAD-RUNG TARGET.  The same proof as a redex: checking an
; application needs its head synthesized, and an unannotated lam head
; cannot synthesize yet.  Today this reads Refuted; same verdict-polarity
; question as above.
!(type:check
     ((lam (f x) (pair x (f x))) (lam x (refl x)))
     (-> (x : u0) (sigma (y : u0) (id u0 x y))))

; ---------------------------------------------------------------------
; Tier C: Nil's chainer -- same recursive shape, nested let to preserve
; nondeterministic branches, Peano depth bound.  The KB types here use
; only -> and u0 so the raw path leaves them inert (id/pair/refl are
; stdlib spellings and would evaluate in bare positions -- one more
; reason search wants the declaration rung's judgment context).
;   A := (-> u0 u0)              a  := identity
;   B := (-> u0 (-> u0 u0))     ab := the K-combinator step
; ---------------------------------------------------------------------

!(bind! &nil-kb (new-space))
!(add-atom &nil-kb (: a (-> u0 u0)))
!(add-atom &nil-kb (: ab (-> (-> u0 u0) (-> u0 (-> u0 u0)))))

; Native synthesis consults this space's exact, revision-bound signature.
!(type:of &nil-kb (ab a))

(= (syn $kb $_ (dtt-goal $prf $ccln))
   (match $kb (: $prf $ccln) (dtt-goal $prf $ccln)))
(= (syn $kb (S $k) (dtt-goal ($prfabs $prfarg) $ccln))
   (let (dtt-goal $prfabs (-> $prms $ccln))
        (syn $kb $k (dtt-goal $prfabs (-> $prms $ccln)))
     (let (dtt-goal $prfarg $prms)
          (syn $kb $k (dtt-goal $prfarg $prms))
       (dtt-goal ($prfabs $prfarg) $ccln))))

; Depth 0: axiom lookup finds `a` at its type.
!(syn &nil-kb Z (dtt-goal $prf (-> u0 u0)))

; Depth 1: the chainer synthesizes the application spine (ab a) at B --
; exactly the HE behavior, over native-spelled types.
!(syn &nil-kb (S Z) (dtt-goal $prf (-> u0 (-> u0 u0))))

; Search widely, believe only checked evidence: every found proof goes to
; native admission.  Search still enumerates relationally; belief belongs to
; the native declaration-indexed checker.
!(let (dtt-goal $prf $ty)
      (syn &nil-kb (S Z) (dtt-goal $prf (-> u0 (-> u0 u0))))
   (type:of &nil-kb $prf))
