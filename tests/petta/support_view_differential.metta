; Seeded scenarios for maintained derived spaces (lib/petta/lib_support.metta).
; Each draws a store, a mix of exec directives and a sequence of operations,
; and prints what every operation answers and what the view and its log hold
; at the end.  The answers are put in a canonical form: the lists `added` and
; `removed` sorted, and a derivation `why` gives reduced to whether its body is
; in the view, since any derivation of the atom from atoms of the view is the
; library's answer.  Running it with the library's own definitions
; (CETTA_SUPPORT_REFERENCE=1) must print the same.
!(import! &self (library lib_support))
!(import! &self (library lib_exec))

(= (next $s) (% (* $s 48271) 2147483647))
(= (pick $s $xs) (index-atom $xs (% $s (size-atom $xs))))

(= (nodes) (a b c d))
(= (colours) (red blue))

; Directive mixes: closures that derive new relations and ones that derive the
; base relation itself, two templates, a three-way join, a constant in a
; pattern, nested heads, a self-justifying rule, and a key two directives share.
(= (menu)
   (((exec one (, (e $x $y)) (, (p $x $y)))
     (exec two (, (p $x $y) (e $y $z)) (, (p $x $z))))
    ((exec sym (, (e $x $y)) (, (e $y $x))))
    ((exec trans (, (e $x $y) (e $y $z)) (, (e $x $z))))
    ((exec paint (, (p $x $y) (l $y $c)) (, (c $x $c) (seen $c))))
    ((exec tri (, (e $x $y) (e $y $z) (e $z $x)) (, (tri $x $y $z))))
    ((exec keep (, (seen $c)) (, (seen $c))))
    ((exec one (, (l $x $c)) (, (p $x $x))))
    ((exec nest (, (e $x $y)) (, (f (g $x) $y))))
    ((exec from (, (e a $y)) (, (from-a $y))))
    ((exec right (, (e $x $y) (p $y $z)) (, (p $x $z))))))

(= (cat $xs $ys)
   (if (== $xs ()) $ys
     (let ($x $more) (decons-atom $xs) (cons-atom $x (cat $more $ys)))))

; three directive groups drawn from the menu (repeats allowed)
(= (directives $s)
   (let* (($s1 (next $s)) ($s2 (next $s1)) ($s3 (next $s2)))
     (cat (pick $s1 (menu)) (cat (pick $s2 (menu)) (pick $s3 (menu))))))

; atoms operations draw from: base relations and atoms some mix derives
(= (candidate $s)
   (let* (($s1 (next $s)) ($s2 (next $s1)) ($s3 (next $s2))
          ($x (pick $s1 (nodes))) ($y (pick $s2 (nodes))))
     (pick $s3 ((e $x $y) (e $x $y) (e $y $x) (l $x red) (l $y blue)
                (p $x $y) (seen red) (c $x blue) (from-a $y)))))

(= (add-new $space $a)
   (if (== (collapse (match $space $a found)) ()) (add-atom $space $a) ()))

; n random base facts, each once
(= (fill $view $s $n)
   (if (== $n 0) $s
     (let* (($s1 (next $s)) ($s2 (next $s1)) ($s3 (next $s2))
            ($x (pick $s1 (nodes))) ($y (pick $s2 (nodes)))
            ($a (pick $s3 ((e $x $y) (e $x $y) (e $x $y) (l $x red) (l $y blue))))
            ($_ (add-new $view $a)))
       (fill $view $s3 (- $n 1)))))

(= (in-view $view $atoms)
   (if (== $atoms ()) True
     (let ($x $more) (decons-atom $atoms)
       (if (== (collapse (match $view $x found)) ()) False (in-view $view $more)))))

(= (canonical $view (added $xs)) (added (sort-atom $xs)))
(= (canonical $view (removed $xs)) (removed (sort-atom $xs)))
(= (canonical $view ()) none)
(= (canonical $view (base)) base)
(= (canonical $view (derived $key $body)) (derived (in-view $view $body)))

; why answers nothing for an atom of the view without a founded derivation,
; which only a write outside the library leaves
(= (why $h $view $a)
   (let $answers (collapse (support:why $h $a))
     (if (== $answers ()) unanswered
       (let ($w $more) (decons-atom $answers) (canonical $view $w)))))

; an atom of the view, drawn from its atoms in sorted order (the order of a
; space is no answer), or a drawn atom when the view is empty
(= (in-view-atom $view $s)
   (let $atoms (sort-atom (collapse (get-atoms $view)))
     (if (== $atoms ()) (candidate $s) (pick $s $atoms))))

; One operation: retract, assert or why of a drawn atom or an atom of the view;
; or a write to the view the library does not make (an atom added outside it,
; which the next retraction finds unfounded, possibly a second copy; or one
; removed).
(= (operation $h $view $s)
   (let* (($s1 (next $s)) ($a (candidate $s1)) ($s2 (next $s1))
          ($k (% $s2 10)))
     (if (< $k 2) (canonical $view (support:retract $h $a))
     (if (== $k 2) (canonical $view (support:retract $h (in-view-atom $view $s2)))
     (if (< $k 5) (canonical $view (support:assert $h $a))
     (if (< $k 7) (why $h $view (in-view-atom $view $s2))
     (if (== $k 7) (why $h $view $a)
     (if (== $k 8) (if (== (% (next $s2) 3) 0)
                       (let $_ (add-atom $view $a) (outside-add $a))
                       (let $_ (add-new $view $a) (outside-add-new $a)))
                   (let $_ (remove-atom $view $a) (outside-remove $a))))))))))

(= (operations $h $view $s $n)
   (if (== $n 0) ()
     (let* (($r (operation $h $view $s))
            ($rest (operations $h $view (next (next (next $s))) (- $n 1))))
       (cons-atom $r $rest))))

(= (scenario $seed)
   (let* (($view (new-space))
          ($s (fill $view (next $seed) 7))
          ($ds (directives $s))
          ($h (support:open $view $ds))
          ($ops (operations $h $view (next (next $s)) 10))
          ((support:view $v $log $d) $h))
     ((seed $seed) (ops $ops)
      (view (sort-atom (collapse (get-atoms $view))))
      (records (sort-atom (collapse (get-atoms $log)))))))

(= (scenarios $seed $n)
   (if (== $n 0) ()
     (let $_ (println! (scenario $seed))
       (scenarios (next (next (next (next $seed)))) (- $n 1)))))

!(scenarios 1 120)
