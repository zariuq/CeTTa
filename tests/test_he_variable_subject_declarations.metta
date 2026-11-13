; A variable subject declares every atom: its type joins the types an atom
; has of its own declarations, and the type an application infers.
(: $x Foo)
(: b Bar)
!(get-type a)
!(let $c (collapse (get-type b)) (sort-atom $c))
!(let $c (collapse (get-type (g 1))) (sort-atom $c))
