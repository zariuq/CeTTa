; PeTTa reflects intrinsic operator atoms as grounded values while ordinary
; symbols and expressions retain their structural metatypes.
!(test (get-metatype +) Grounded)
!(test (get-metatype 1) Grounded)
!(test (get-metatype a) Symbol)
!(test (get-metatype (a b)) Expression)

; repra is PeTTa's native term-to-Symbol observer.  It evaluates its input,
; renders compound values with PeTTa's Prolog-compatible spelling, and the
; enclosing metatype observer must inspect that Symbol rather than its source.
!(repra (+ 1 2))
!(repra (1 2 3))
!(repra foo-bar)
!(repra (foo-bar True "abc"))
!(get-metatype (repra (1 2 3)))
!(== (get-metatype (repra (1 2 3))) Expression)
