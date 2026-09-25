(= (regular-syntax-identity) (lam x x))

!(type:formed u1)
!(type:formed (-> (x y : u0) u0))

!(type:check (lam x x) (-> u0 u0))
!(type:analyze (lam x x) (-> u0 u0))
!(type:check (lam (x y) x) (-> u0 u0 u0))
!(type:check (lam (x x) x) (-> u0 u0 u0))
!(type:check (lam _ (idx 0)) (-> u0 u0))
!(type:check (lam (_ x) (idx 1)) (-> u0 u0 u0))
!(type:check (lam @1 *@1) (-> u0 u0))
!(type:check (lam @_ *@_) (-> u0 u0))
!(type:check (lam @(mm-var "ph") *@(mm-var "ph")) (-> u0 u0))
!(type:check (lam λ λ) (-> u0 u0))
!(type:check (lam (x y) x) (-> ((x : u0) (y : u0)) u0))
!(type:check (lam (x y) x) (-> (x y : u0) u0))
!(type:check (lam (x y) x) (-> (x y : u0 u0) u0))
!(type:check (lam (x y) x) (-> (: x u0) (: y u0) u0))
!(type:check (lam _ (idx 0)) (-> (_ : u0) u0))
!(type:may (regular-syntax-identity) (-> u0 u0))
!(type:must (regular-syntax-identity) (-> u0 u0))

!(type:check (lam x u0) (-> u0 u0))
!(type:check (lam x x) u0)
!(type:check (lam $x $x) (-> u0 u0))
!(type:check (lam 1 1) (-> u0 u0))
!(type:check (lam () u0) (-> u0 u0))
!(type:check (lam _ (idx 1)) (-> u0 u0))
!(type:check (lam (x y) x) (-> (x y : u0 u0 u0) u0))
!(type:check (lam x x) (-> ($t : u0) u0))
!(type:formed (-> (x y : u0 u0 u0) u0))

; A written binder domain is checked against the expected domain
; (written_domains.metta covers the rest).  An unknown body stays outside this
; proved syntax and is not refuted merely for failing fragment membership.
!(type:check (lam (x : u0) x) (-> u0 u0))
!(type:check (lam x (unknown x)) (-> u0 u0))

; The quotation of a bound name is the name, not the value it binds, and a
; name has no type in this fragment.
!(type:check (lam @(mm-var "ph") @(mm-var "ph")) (-> u0 u0))
!(type:check (lam x @x) (-> u0 u0))
