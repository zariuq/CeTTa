(= (sq $x) (* $x $x))

; Without an imported capability this spelling is ordinary MeTTa data.
!(memoize sq)
!(sq 3)
