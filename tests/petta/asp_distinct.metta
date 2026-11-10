; asp:distinct in the PeTTa lane: Hall's condition over groups.
!(import! &self asp)
; three keys, three values: distinct representatives
!(asp:distinct ((a 1 2) (b 2 3) (c 1 3)))
; three keys share two values: a violator, whatever the fourth key does
!(asp:distinct ((a 1 2) (b 1 2) (c 1 2) (d 3 4)))
; a key with no open value
!(asp:distinct ((a) (b 1)))
; no keys
!(asp:distinct ())
; values are any ground atoms
!(asp:distinct ((a (on x) (on y)) (b (on x))))
; pigeons and holes: four pigeons, three holes
!(asp:distinct ((p1 h1 h2 h3) (p2 h1 h2 h3) (p3 h1 h2 h3) (p4 h1 h2 h3)))
; a search that keeps going only while the open values allow distinct values
!(case (asp:distinct ((r1 1 2) (r2 2) (r3 1 3))) (((sdr $s) $s) ((hall $k $v) (none $k))))
; groups that are not ground data are not this library's to answer
!(asp:distinct ((a $x)))
!(asp:distinct foo)
