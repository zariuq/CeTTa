; Evaluation reduces a call by an admitted definition only to a term of the
; evaluated space's own vocabulary.  A definition whose body quantifies, or
; states an equation, is stored with the source's polymorphic head, the
; signature's `all` or `eq`; the proof checker's specialized instances of it
; (a quantifier or equality at one domain) are internal names no program
; declares, and a reduct is never spelled with them.  This space does not
; declare the quantifier or the equality, so such a call stays as written.
(: prop (u 0))
(: imp (-> prop (-> prop prop)))
(: A prop)
(: B prop)
!(set:define and (-> prop prop prop)
   (= (and $p $q) (all prop (lam r (imp (imp $p (imp $q r)) r)))))
!(set:define same (-> prop prop) (= (same $p) (eq prop $p $p)))
!(and A B)
!(same A)

; A definition over the space's own constants reduces as before.
(: nat (u 0))
(: z nat)
(: s (-> nat nat))
!(set:define twice (-> nat nat) (= (twice $n) (s (s $n))))
!(twice z)
!(twice (twice z))
