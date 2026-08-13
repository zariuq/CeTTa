; Direct clause slots may use the live trail only for an admitted query shape.
; This open row shares variables across traversal and removal.  The optimized
; and isolated matchers must both remove its exact occurrence.
(= (collect-vars $term $acc)
   (if (is-var $term)
       (cons $term $acc)
       (if (is-expr $term)
           (collect-vars-list $term $acc)
           $acc)))

(= (collect-vars-list $terms $acc)
   (if (== $terms ())
       $acc
       (collect-vars
          (car-atom $terms)
          (collect-vars-list (cdr-atom $terms) $acc))))

; Query-visible variables may acquire structured values, but an unbound
; query/rule variable pair must retain the query's identity in the result.
(= (slot-bind-structure (pair $left $right))
   (bound $left $right))
(= (slot-alias $value) $value)

; Exercise both nonlinear orientations.  The first constrains two caller
; variables to one rule slot; the second maps two rule slots to one caller.
(= (slot-repeat-rule $same $same) (same $same))
(= (slot-repeat-caller $left $right) (pair $left $right))

; Multiplicity is occurrence-based even when two clauses are identical.
(= (slot-duplicate $value) (duplicate $value))
(= (slot-duplicate $value) (duplicate $value))

; The first occurrence creates a cyclic substitution for a repeated caller
; variable and must fail before the second occurrence is tried.
(= (slot-cycle $value (wrap $value)) bad)
(= (slot-cycle $left $right) good)

!(add-atom &clause-slot-admission-space
   (stored-row 0 (shared-form $left $right)))

!(let*
   (((stored-row $score $form)
       (once (match &clause-slot-admission-space
          (stored-row $candidate-score $candidate-form)
          (stored-row $candidate-score $candidate-form))))
    ($_capture (once (collect-vars $form ())))
    ($_remove (remove-atom &clause-slot-admission-space
       (stored-row $score $form))))
   (clause-slot-rows-left
      (size (collapse
         (match &clause-slot-admission-space $row $row)))))

!(let* (((bound $left $right)
          (slot-bind-structure $shape)))
   (slot-structured-binding
      (== $shape (pair $left $right))))

!(let* (($answer (slot-alias $outer)))
   (slot-variable-alias (== $answer $outer)))

!(let* (((same $value)
          (slot-repeat-rule $left $right)))
   (slot-repeated-rule
      (== $left $right)
      (== $value $left)))

!(let* (((pair $left $right)
          (slot-repeat-caller $shared $shared)))
   (slot-repeated-caller
      (== $left $right)
      (== $left $shared)))

!(slot-duplicate-multiplicity
   (size (collapse (slot-duplicate marker))))

!(slot-cycle-fallback
   (collapse (slot-cycle $shared $shared)))
