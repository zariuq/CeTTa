; Stable authored intake. Exact open-context beta/eta correspondence remains
; in the regular-kernel C test, where the intrinsic context is explicit.
!(type:of u0)
!(type:check (lam x x) (-> u0 u0))
!(type:eq (-> (x : u0) u0) (-> (y : u0) u0))
!(type:eq Foo Foo)
!(type:of (idx 0))
