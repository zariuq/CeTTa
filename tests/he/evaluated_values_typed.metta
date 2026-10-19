; A value is not evaluated again, but he still checks it against a declared
; result type; he-compat answers it unchecked, as Rust HE does.
(: Foo Type)
(: mk (-> Symbol Foo))
(: f2 (-> %Undefined% Number))
(= (f2 $x) $x)
!(f2 (mk a))
