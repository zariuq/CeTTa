; K0: leftover dest hole against an interned compound open head.
; Query `(: $p T)` matches interned `(: ax T)` (dest binds a symbol) and
; interned `(: (mp $f $x) T)` (dest binds the open mp spine).  No recursion.
; J1–J4 never bind a leftover dest to interned `(mp $f $x)`.
(= (k0 (: ax T)) (hit ax))
(= (k0 (: (mp $f $x) T)) (hit (mp $f $x)))
!(k0 (: $p T))
