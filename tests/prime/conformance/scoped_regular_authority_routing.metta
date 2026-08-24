; Outside the proved regular fragment, a true equality may be established by
; the ambient language, but fragment-membership failures never become false.
; Exact scoped-wrapper routing is qualified through the C kernel gate.
!(type:eq Foo Foo)
!(type:eq (lam x Foo) (lam x Foo))
!(type:eq (-> (x : Foo) u0) (-> (y : Foo) u0))
!(type:eq (idx 0) (idx 0))
!(type:eq u0 u0)
!(type:of Foo)
!(type:check Foo Foo)
