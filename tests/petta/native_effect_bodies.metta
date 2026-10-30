; The body of a let chain a match makes: data, answered as it is, or itself
; an effect made after the others; any other call, ground or not, is
; evaluated, never answered as written.
(e 1) (e 2) (e 3)
!(bind! &w (new-space set))
!(bind! &g (new-space set))
!(collapse (match &self (e $x) (let $_ (add-atom &w (a $x)) (add-atom &g done))))
!(collapse (match &g $y $y))
!(collapse (match &self (e $x) (let $_ (add-atom &w (b $x)) (add-atom &g (c $x)))))
!(sort-atom (collapse (match &g $y $y)))
!(collapse (match &self (e $x) (let* (($u (add-atom &w (d $x))) ($v (remove-atom &w (a $x)))) (remove-atom &g (c $x)))))
!(sort-atom (collapse (match &w $y $y)))
!(sort-atom (collapse (match &g $y $y)))
(= (twice $x) (* 2 $x))
!(collapse (match &self (e $x) (let $_ (add-atom &w (f $x)) (twice 21))))
!(collapse (match &self (e $x) (let $_ (add-atom &w (h $x)) (twice $x))))
!(size-atom (collapse (match &self (e $x) (let $_ (add-atom &w (k $x)) (add-atom &w (k $x))))))
!(sort-atom (collapse (match &w $y $y)))
