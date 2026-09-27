; An unannotated lambda applied to several arguments takes each binder's
; type from its argument, as a lambda applied to one argument does.  The
; judgments read the application and decide it.
!(bind! &s (new-space))
!(add-atom &s (: A (u 0)))
!(add-atom &s (: B (u 0)))
!(add-atom &s (: a A))
!(add-atom &s (: b B))
!(type:eq &s ((lam Z (lam e Z)) A a) A)
!(type:eq &s ((lam Z (lam e Z)) A a) B)
!(type:of &s ((lam Z (lam e Z)) A a))
!(type:check &s ((lam Z (lam e Z)) A a) (u 0))
!(type:eq &s ((lam x (lam y x)) a b) a)
!(type:eq &s ((lam x (lam y y)) a b) b)
!(type:check &s ((lam x (lam y y)) a b) B)
!(type:check &s ((lam x (lam y y)) a b) A)
!(type:eq &s ((lam x (lam y (lam z x))) a b a) a)
; One argument, as before.
!(type:eq &s ((lam Z Z) A) A)
