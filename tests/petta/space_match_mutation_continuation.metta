; A value selected from a space remains the exact occurrence value when it is
; consumed by a later mutation.  The cases vary the location expression,
; key shape, duplicate multiplicity, and continuation form.

!(add-atom &mutation-symbol (row old))
!(let $value
      (match &mutation-symbol (row $_) (row $_))
      (remove-atom &mutation-symbol $value))
!(test (collapse (get-atoms &mutation-symbol)) ())

!(add-atom &mutation-expression ((key nested) old))
!(let* (($value
           (match &mutation-expression (($key $_) $_) (($key $_) $_)))
         ($_removed (remove-atom &mutation-expression $value)))
        True)
!(test (collapse (get-atoms &mutation-expression)) ())

!(add-atom &mutation-duplicates (row same))
!(add-atom &mutation-duplicates (row same))
!(let $value
      (match &mutation-duplicates (row $_) (row $_))
      (remove-atom &mutation-duplicates $value))
!(test (collapse (get-atoms &mutation-duplicates)) ())

(= (mutation-location) &mutation-derived-location)
!(add-atom &mutation-derived-location (row old))
!(let $value
      (match (mutation-location) (row $_) (row $_))
      (remove-atom (mutation-location) $value))
!(test (collapse (get-atoms (mutation-location))) ())

(= (remove-selected $space $pattern)
   (let $value
        (match $space $pattern $pattern)
        (remove-atom $space $value)))
!(add-atom &mutation-equation (row old))
!(remove-selected &mutation-equation (row $_))
!(test (collapse (get-atoms &mutation-equation)) ())

; A nonlinear removal pattern contracts only occurrences whose two fields
; can share one value.  Every surviving occurrence retains authored order.
!(add-atom &mutation-nonlinear (tagged-value first))
!(add-atom &mutation-nonlinear (pair a a))
!(add-atom &mutation-nonlinear (pair a b))
!(add-atom &mutation-nonlinear (tagged-value second))
!(add-atom &mutation-nonlinear (pair c c))
!(add-atom &mutation-nonlinear (pair c d))
!(remove-atom &mutation-nonlinear (pair $same $same))
!(test
    (collapse (match &mutation-nonlinear (pair $left $right)
                     (pair $left $right)))
    ((pair a b) (pair c d)))
!(test
    (collapse (match &mutation-nonlinear (tagged-value $value) $value))
    (first second))

; Stored variables are local to their occurrence.  A closed removal pattern
; may match such an open occurrence without capturing any ambient variable.
!(add-atom &mutation-open-stored (open $stored))
!(add-atom &mutation-open-stored (closed keep))
!(remove-atom &mutation-open-stored (open concrete))
!(test
    (collapse (match &mutation-open-stored (closed $value) $value))
    (keep))

; PeTTa removal is defined on nonempty expression patterns.  A bare variable
; does not acquire a whole-Space deletion meaning.
!(add-atom &mutation-wildcard (datum one))
!(add-atom &mutation-wildcard (datum one))
!(add-atom &mutation-wildcard (zero))
!(add-atom &mutation-wildcard (= (local-rule $x) (answer $x)))
!(remove-atom &mutation-wildcard $anything)
!(test
    (collapse (match &mutation-wildcard (datum $value) $value))
    (one one))
!(test
    (collapse (match &mutation-wildcard (zero) zero))
    (zero))

; A pattern with no unifier is the identity transformation.
!(add-atom &mutation-negative (row keep-1))
!(add-atom &mutation-negative (row keep-2))
!(remove-atom &mutation-negative (other $value))
!(test
    (collapse (match &mutation-negative (row $value) $value))
    (keep-1 keep-2))
