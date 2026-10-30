(: keep-name (-> Proc Proc))
(: fresh-name (-> Proc))

(= (keep-name $@(meta "x")) $@(meta "x"))
(= (fresh-name) $@(generated "y"))
