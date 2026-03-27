!(import! &self pverify-utils)

!(bind! &subst_probe (new-space))
!(add-atom &subst_probe ("P" ("t" "mm.=" "r")))
!(unify &subst_probe ("P" $x) $x nope)
!(unify &subst_probe (P $x) $x nope)
!(apply_subst &subst_probe ("|-" "P"))
