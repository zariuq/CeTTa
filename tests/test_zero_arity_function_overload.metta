(: dup (-> Atom (->)))
(: dup (-> (->)))
(= (dup $x) ok)
!(assertEqual (dup a) ok)

(: identity-overloaded (-> Atom Atom))
(: identity-overloaded (-> (->)))
(= (identity-overloaded $x) $x)
!(assertEqual (identity-overloaded z) z)
