; Prime typing is ordinary MeTTa data and control under one public namespace.
; Unknown and resource-incomplete judgments remain inert; only a proved
; negative becomes False.

!(type:formed u0)
!(type:formed (-> u0 u0))
!(type:formed 7)

!(type:of u0)
!(type:of (lam x x))

!(type:check (lam x x) (-> u0 u0))
!(type:check (lam x x) u0)
!(type:analyze (lam x x) (-> u0 u0))

; Generated syntax stays quoted until the explicit splice enters the same
; checking judgment as an authored term.
!(let $candidate (quote (lam x x))
   (type:check (unquote $candidate) (-> u0 u0)))
!(let $candidate (quote (lam x x))
   (type:check (unquote $candidate) u0))
; Without the explicit splice, the quotation remains data and the authority
; declines rather than silently checking its contents.
!(type:check (quote (lam x x)) (-> u0 u0))

!(type:eq u0 u0)
; The cumulative tower forms both universes, so their distinct levels provide
; a checked obstruction rather than an abstention.
!(type:eq u0 u1)

; These two public declaration names are drafts.  Their distinction is not:
; theorem checks a supplied proof, while prove checks a relational stream of
; candidates before publishing the declaration.
!(type:theorem identity (-> u0 u0) (lam x x))
!(type:of identity)
!(type:check identity (-> u0 u0))
!(type:theorem ill-typed u0 (lam x x))
!(type:theorem identity u1 u0)

(= (identity-candidate) (lam x x))
(= (identity-candidate) u0)
!(type:prove found-identity (-> u0 u0) (identity-candidate))
!(type:of found-identity)
