; Data supplies holding: a parameter typed Data receives its argument as
; written, and the call site does not change.  Held syntax stays held
; through return, deconstruction and construction; explicit evaluation runs
; it; size is an ordinary number.
(: hold (-> (Data Atom) (Data Atom)))
(= (hold $x) $x)
(: tail-of (-> (Data Atom) (Data Atom)))
(= (tail-of $x) (cdr-atom $x))
(: head-of (-> (Data Atom) (Data Atom)))
(= (head-of $x) (car-atom $x))
(: build (-> (Data Atom) (Data Atom)))
(= (build $x) (cons-atom wrap $x))
(: run-held (-> (Data Atom) $r))
(= (run-held $x) (eval $x))
(: pick (-> (Data Atom) (Data Atom)))
(= (pick $x) (let ($h $t) (decons-atom $x) $h))
(: rest (-> (Data Atom) (Data Atom)))
(= (rest $x) (let ($h $t) (decons-atom $x) $t))
(= (f $y) (g $y))
(= (g $y) (pair $y $y))

; Holding.
!(hold ((lam x x) payload))
!(hold (f nil))

; Holding survives return, deconstruction and construction.
!(tail-of (mapf (lam y (f (g y))) nil))
!(head-of (f (g nil)))
!(build ((lam x x) payload))
!(rest ((lam x x) payload))
!(pick ((lam x x) payload))
!(let $d (hold ((lam x x) payload)) (tail-of $d))

; Held syntax is a value for the ordinary observers.
!(size-atom (hold (f (g nil))))
!(== (hold (f nil)) (hold (f nil)))
!(== (hold (f nil)) (hold (g nil)))

; Shape, indexing and unification read held syntax; bound parts stay held.
(: shape (-> (Data Atom) $r))
(= (shape $x) (get-metatype $x))
(: second (-> (Data Atom) (Data Atom)))
(= (second $x) (index-atom $x 1))
(: body-of (-> (Data Atom) (Data Atom)))
(= (body-of $x) (unify $x (lam $y $b) $b none))
!(shape (f nil))
!(shape nil)
!(second (f (g nil)))
!(body-of (lam z (f z)))
!(size-atom (body-of (lam z (f z))))

; Explicit evaluation runs held code.
!(run-held ((lam x x) payload))
!(let $d (hold (f nil)) (eval $d))
!(run-held (f nil))

; Passing a held value on keeps it held once, not twice.
(: hold-again (-> (Data Atom) (Data Atom)))
(= (hold-again $d) (hold $d))
!(hold-again (f nil))
!(size-atom (hold-again (f nil)))

; Holding survives storage.
!(add-atom &self (stored (hold (f nil))))
!(match &self (stored $s) $s)
!(match &self (stored $s) (size-atom $s))
