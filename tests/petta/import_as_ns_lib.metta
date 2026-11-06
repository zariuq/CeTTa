; Every definition here shares the namespace ns, which an import prefix
; replaces.
(= (ns:one) 1)
(= (ns:succ $x) (+ $x (ns:one)))
