; A grounder's program is made where it lives on: about 667,000 integrity
; constraints over 2,000 choices, grounded by clingo and solved by the asp
; library.  With CETTA_PETTA_MACHINE_STATS=1 the machine reports the bytes its
; collections copied, which stay far below the program's size.
!(import! &self asp)
!(import! &self clingo)
!(let $p (clingo:ground "n(1..2000). {p(X)} :- n(X). :- p(X), p(Y), X < Y, (X + Y) \\ 3 == 0.")
   (let $r (asp:solve $p) (verdict (size-atom $p) (car-atom $r))))
