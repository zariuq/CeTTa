; A set space holds an atom with variables once, whatever its variables are
; named.  It knows which atoms it holds by keys made from them, which no
; row holds: a collection between statements must keep them, or a variant
; added after it would be taken for a new atom.
!(bind! &s (new-space set))
!(add-atom &s (rule $x (f $x)))
!(add-atom &s (pair $a $b))
!(add-atom &s (pair 1 2))
!(add-atom &s (rule $y (f $y)))
!(add-atom &s (pair $c $d))
!(add-atom &s (pair $c $c))
!(size-atom (collapse (match &s $atom $atom)))
!(add-atom &s (rule $w (f $w)))
!(add-atom &s (pair $e $e))
!(size-atom (collapse (match &s $atom $atom)))
!(size-atom (collapse (match &s (rule $p $q) $p)))
