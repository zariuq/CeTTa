(: dup (-> Atom (->)))
(: dup (-> (->)))
(= (dup $x) ok)
!(assertEqual (dup a) ok)

(: identity-overloaded (-> Atom Atom))
(: identity-overloaded (-> (->)))
(= (identity-overloaded $x) $x)
!(assertEqual (identity-overloaded z) z)

(: zero-number (-> Number))
(= (zero-number) 7)
!(assertEqual (get-type (zero-number)) Number)
!(assertEqual (zero-number) 7)
