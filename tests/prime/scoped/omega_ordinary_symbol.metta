; Only inside a universe (u ...) are omega, its Greek letter, +, * and ^ level
; syntax.  Everywhere else they are what they were before: ordinary symbols
; and ordinary arithmetic.  A program may name its own things omega.

; Symbols and data.
!(car-atom (omega ω))
!(cdr-atom (omega ω))
!(== omega ω)
!(== omega omega)

; Equations at these names.
(= (omega) 42)
(= (twice omega) (omega omega))
(= (name ω) greek-omega)
!(omega)
!(twice omega)
!(name ω)
!(name omega)

; Arithmetic outside a universe.
!(+ 1 2)
!(* 2 3)
(= (plus-one $n) (+ $n 1))
!(plus-one 41)

; Declared constants named omega: the space's names, not levels.
!(bind! &s (new-space))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
!(add-atom &s (: omega num))
!(add-atom &s (: ω num))
!(type:of &s omega)
!(type:of &s ω)
!(type:check &s (suc omega) num)
!(type:of &s (suc ω))
!(type:check &s omega (u omega))

; In the universe position the same spelling is the level, whatever the
; space declares under that name.
!(type:of &s (u omega))
!(type:check &s num (u omega))
!(type:of &s (-> (X : (u omega)) num))
