!(import! &self system)
; A deliberate entry exit is not proof that later declared queries completed.
!(system:exit 0)
!(+ 40 2)
