(= (hide $value) (empty))

!(hide ((add-atom &self (edge a b))
        (add-atom &self (edge b c))
        (add-atom &self (edge a d))
        (add-atom &self (edge d e))
        (add-atom &self (edge noise nowhere))
        (add-atom &self (dup-edge a b))
        (add-atom &self (dup-edge a b))
        (add-atom &self (dup-edge b c))
        (add-atom &self (consume-edge p q))
        (add-atom &self (consume-edge q r))
        (add-atom &self (consume-edge s t))
        (add-atom &self (consume-edge t u))))

(= (flat-conjunction-answers)
   (collapse
     (match &self (, (edge $source $middle)
                      (edge $middle $target))
            ($source $middle $target))))

; A conjunction denotes an occurrence bag.  These observations constrain its
; multiplicity and members without making declaration order part of the test.
!(test (length (flat-conjunction-answers)) 2)
!(test (is-member (a b c) (flat-conjunction-answers)) true)
!(test (is-member (a d e) (flat-conjunction-answers)) true)

; A nested comma is one literal child occurrence; it is not recursively
; flattened into the outer conjunction.
(= (nested-conjunction-answers)
   (collapse
     (match &self (, (edge $source $middle)
                      (, (edge $middle $target)))
            ($source $middle $target))))

!(test (length (nested-conjunction-answers)) 0)

; Duplicate source occurrences remain duplicate joined answers.
(= (duplicate-conjunction-answers)
   (collapse
     (match &self (, (dup-edge $source $middle)
                      (dup-edge $middle $target))
            ($source $middle $target))))

!(test (length (duplicate-conjunction-answers)) 2)
!(test (once (is-member (a b c) (duplicate-conjunction-answers))) true)

; A conjunction over another space stays on the general nested-match
; realization.  This is the negative control for the direct-&self cursor.
!(bind! &conjunction-other (new-space))
!(add-atom &conjunction-other (other-edge a b))
!(add-atom &conjunction-other (other-edge b c))

!(test
   (length
     (collapse
       (match &conjunction-other
              (, (other-edge $source $middle)
                 (other-edge $middle $target))
              ($source $middle $target))))
   1)

; The template runs after the complete conjunction, once per joined answer.
(= (publish-conjunction-answers)
   (collapse
     (match &self (, (edge $source $middle)
                      (edge $middle $target))
            (add-atom &self (joined $source $middle $target)))))

!(hide (publish-conjunction-answers))
!(test (length (collapse
                 (match &self (joined $source $middle $target)
                        ($source $middle $target))))
       2)

; Source removals remain ordered effects after each complete join.  The
; cursor changes term demand, not the logical-update view of the match.
(= (consume-conjunction-answers)
   (collapse
     (match &self (, (consume-edge $source $middle)
                      (consume-edge $middle $target))
            ((add-atom &self (consumed $source $middle $target))
             (remove-atom &self (consume-edge $source $middle))
             (remove-atom &self (consume-edge $middle $target))))))

!(hide (consume-conjunction-answers))
!(test (length (collapse
                 (match &self (consumed $source $middle $target)
                        ($source $middle $target))))
       2)
!(test (length (collapse
                 (match &self (consume-edge $source $target)
                        ($source $target))))
       0)

!(collapse (match &self (,) seed))
