; FORK-ADM: run first, or admit first?  A program may run before it is
; formed; a published claim needs the formation-sensitive judgment.

; Runs, and is even useful, without any admission.
(= (set:twice $f $x) ($f ($f $x)))
!(set:twice Power Empty)
; The mathematical face declines the same name: no authority from spelling.
!(set:check (set:twice Power Empty) set)

; Operational typing observes what a producer actually yields...
(= (candidate) (superpose ((lam x x) u0)))
!(type:may (candidate) (-> u0 u0))
!(type:must (candidate) (-> u0 u0))
; ...while the logical judgment checks one term against a formed type.
!(type:check (lam x x) (-> u0 u0))
!(type:check (lam x x) u0)
; A proposition-valued codomain is not a type; the judgment abstains rather
; than admitting an unformed arrow.
!(set:formed (-> (x : set) (In x x)))
