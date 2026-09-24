; Renaming a binder must not change the value. The argument y is free in
; (lam y x) after x is replaced, so that inner binder cannot capture it.
!(((lam x (lam y x)) y) z)
!(((lam x (lam w x)) y) z)
!(((lam x (lam x x)) y) z)
!((lam x (lam y x)) y)
!((lam x (quote (lam y x))) y)
!((lam x (quote x)) y)
