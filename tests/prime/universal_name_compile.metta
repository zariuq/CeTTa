(: keep-name (-> Atom Atom))
(: fresh-name (-> Atom))

(= (keep-name $@(meta "x")) $@(meta "x"))
(= (fresh-name) $@(generated "y"))
