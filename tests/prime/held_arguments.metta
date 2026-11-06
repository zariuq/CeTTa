; A parameter declared Atom holds its argument as data.  Under an Atom range
; the body comes back as written, whether it returns the parameter, wraps it
; or passes it to a call; under %Undefined% the body is evaluated, and the
; argument it returns with it.  (f a) reduces to 5, so a held (f a) that ran
; would answer 5.
(= (f a) 5)
(: held-bare (-> Atom Atom))
(= (held-bare $x) $x)
(: held-second (-> Atom Atom Atom))
(= (held-second $x $y) $y)
(: held-wrapped (-> Atom Atom))
(= (held-wrapped $x) (wrap $x))
(: held-called (-> Atom Atom))
(= (held-called $x) (h $x))
(: held-undefined (-> Atom %Undefined%))
(= (held-undefined $x) $x)
!(held-bare (+ 1 2))
!(held-bare (f a))
!(held-second 0 (f a))
!(held-wrapped (f a))
!(held-called (f a))
!(held-undefined (f a))
