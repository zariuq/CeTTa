; Data supplies holding: a parameter typed Data receives its argument as
; written, and the call site does not change.  Held syntax stays held
; through return, deconstruction and construction; explicit evaluation runs
; it; size is an ordinary number.
(: hold (-> Atom Atom))
(= (hold $x) $x)
(: tail-of (-> Atom Atom))
(= (tail-of $x) (cdr-atom $x))
(: head-of (-> Atom Atom))
(= (head-of $x) (car-atom $x))
(: build (-> Atom Atom))
(= (build $x) (cons-atom wrap $x))
(: run-held (-> Atom $r))
(= (run-held $x) (eval $x))
(: pick (-> Atom Atom))
(= (pick $x) (let ($h $t) (decons-atom $x) $h))
(: rest (-> Atom Atom))
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
(: shape (-> Atom $r))
(= (shape $x) (get-metatype $x))
(: second (-> Atom Atom))
(= (second $x) (index-atom $x 1))
(: body-of (-> Atom Atom))
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
(: hold-again (-> Atom Atom))
(= (hold-again $d) (hold $d))
!(hold-again (f nil))
!(size-atom (hold-again (f nil)))

; Produce the held value before passing it to the data-only storage operation.
!(let $held (hold (f nil))
   (add-atom &self (stored $held)))
!(match &self (stored $s) $s)
!(match &self (stored $s) (size-atom $s))

; Written code is stored as data. Opening it is a separate evaluation request.
!(add-atom &self (stored-source (hold (f nil))))
!(match &self (stored-source $s) $s)
!(match &self (stored-source $s) (eval $s))
