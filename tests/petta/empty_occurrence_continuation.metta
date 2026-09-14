; The empty expression is ordinary PeTTa data.  These equations exercise its
; transport through independent evaluation contexts so it cannot be confused
; with the absence of a computational result.

(= (empty-value) ())
(= (pass-value $value) $value)
(= (choose-value $empty) (if $empty () payload))

(= (let-from-call)
   (let $value (empty-value)
        (if (== $value ()) empty-kept empty-lost)))

(= (let-from-parameter $input)
   (let $value (pass-value $input)
        (if (== $value ()) empty-kept nonempty-kept)))

(= (nested-let-star)
   (let* (($first (empty-value))
          ($second (pass-value $first)))
         (if (== $second ()) empty-kept empty-lost)))

(= (empty-through-branch)
   (let $value (choose-value True)
        (if (== $value ()) empty-kept empty-lost)))

(= (empty-through-fold)
   (let $value
        (foldall union-atom
                 (match &missing-empty-occurrence-space ($item) $item)
                 ())
        (if (== $value ()) empty-kept empty-lost)))

(= (empty-fold-value)
   (foldall union-atom
            (match &missing-empty-occurrence-space ($item) $item)
            ()))

(= (empty-fold-direct-equality)
   (== (foldall union-atom
                (match &missing-empty-occurrence-space ($item) $item)
                ())
       ()))

(= (empty-fold-call-equality)
   (== (empty-fold-value) ()))

(= (empty-fold-bound-value)
   (let $value (empty-fold-value) $value))

(= (empty-fold-bound-is-expression)
   (let $value (empty-fold-value) (is-expr $value)))

(= (empty-as-argument)
   (pass-value (empty-value)))

; Independent producers of the same ordinary empty-expression value.  Each
; crosses a binding boundary before equality observes it; the paired nonempty
; cases ensure that no producer is being collapsed indiscriminately.

(= (empty-through-collapse)
   (let $value
        (collapse
          (match &missing-empty-occurrence-space ($item) $item))
        (if (== $value ()) empty-kept empty-lost)))

(= (nonempty-through-collapse)
   (let $value (collapse (superpose (payload)))
        (if (== $value (payload)) nonempty-kept nonempty-lost)))

(= (empty-through-filter)
   (let $value (filter-atom (only) $item False)
        (if (== $value ()) empty-kept empty-lost)))

(= (nonempty-through-filter)
   (let $value (filter-atom (only) $item True)
        (if (== $value (only)) nonempty-kept nonempty-lost)))

(= (empty-through-unique)
   (let $value (unique-atom ())
        (if (== $value ()) empty-kept empty-lost)))

(= (nonempty-through-unique)
   (let $value (unique-atom (x x y))
        (if (== $value (x y)) nonempty-kept nonempty-lost)))

(= (empty-through-tail)
   (let $value (cdr-atom (only))
        (if (== $value ()) empty-kept empty-lost)))

(= (empty-through-sort)
   (let $value (sort-atom ())
        (if (== $value ()) empty-kept empty-lost)))

(= (nonempty-through-sort)
   (let $value (sort-atom (b a))
        (if (== $value (a b)) nonempty-kept nonempty-lost)))

(= (nonempty-through-fold)
   (let $value
        (foldall union-atom
                 (match &missing-empty-occurrence-space ($item) $item)
                 (seed))
        (if (== $value (seed)) nonempty-kept nonempty-lost)))

(= (nested-empty-observation)
   (let $value (empty-fold-value)
        (if (== (box $value) (box ())) empty-kept empty-lost)))

!(test (empty-value) ())
!(test (let-from-call) empty-kept)
!(test (let-from-parameter ()) empty-kept)
!(test (let-from-parameter payload) nonempty-kept)
!(test (nested-let-star) empty-kept)
!(test (empty-through-branch) empty-kept)
!(test (empty-as-argument) ())
!(test (empty-fold-value) ())
!(test (empty-fold-direct-equality) True)
!(test (empty-fold-call-equality) True)
!(test (empty-fold-bound-value) ())
!(test (empty-fold-bound-is-expression) True)
!(test (empty-through-fold) empty-kept)
!(test (empty-through-collapse) empty-kept)
!(test (nonempty-through-collapse) nonempty-kept)
!(test (empty-through-filter) empty-kept)
!(test (nonempty-through-filter) nonempty-kept)
!(test (empty-through-unique) empty-kept)
!(test (nonempty-through-unique) nonempty-kept)
!(test (empty-through-tail) empty-kept)
!(test (cdr-atom (1 2 3)) (2 3))
!(test (empty-through-sort) empty-kept)
!(test (nonempty-through-sort) nonempty-kept)
!(test (nonempty-through-fold) nonempty-kept)
!(test (nested-empty-observation) empty-kept)
