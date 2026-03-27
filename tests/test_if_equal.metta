; HE-backed if-equal uses alpha-equivalence on the first two atoms and keeps
; the branches lazy, so dead branches are not evaluated.

!(if-equal (R $x $x) (R $a $b) yes no)
!(if-equal (R $x $x) (R $a $a) yes no)
!(if-equal foo foo ok (decons-atom ()))
!(if-equal foo bar (decons-atom ()) ok)
