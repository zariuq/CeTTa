!(bind! &pm (new-space pathmap))
!(add-atom &pm (edge a b))
!(add-atom &pm (edge a c))

!(test (get-type &pm) (Space pathmap))
!(test (collapse (match &pm (edge a $x) $x)) (b c))
!(test (let $_ (add-atom &pm (color b blue))
        (collapse (match &pm (color $who blue) $who)))
       (b))

(= (self-pathmap-probe) ok)
!(test (self-pathmap-probe) ok)
