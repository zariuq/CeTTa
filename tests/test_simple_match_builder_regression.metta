; Regression for builder-backed one-way simple_match sites.
; Repeated variables in case/switch/let patterns must still enforce co-reference.

!(assertEqual
  (case (pair a a)
    (((pair $x $x) (same $x))
     ((pair $x $y) (different $x $y))))
  (same a))

!(assertEqual
  (switch (pair b b)
    (((pair $x $x) ok)
     ((pair $x $y) bad)))
  ok)

!(assertEqual
  (let (pair $x $x) (pair c c) (same $x))
  (same c))
