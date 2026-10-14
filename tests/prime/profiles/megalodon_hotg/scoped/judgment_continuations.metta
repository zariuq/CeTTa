(hol:profile megalodon-hotg)

; Judgment operands are syntax. Binding a computed value must preserve that
; contract in every evaluator continuation, including relational space lookup.
; Every set is a member of its power set: the law of the power set at the set
; itself, whose second half, read from right to left, applies to the proof
; that every member of the set is a member of it.
!(hol:theorem power-self (all set (lam x (In x (Power x))))
   (pf:all-intro
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known powerLaw) (pf:var 0)) (pf:var 0))
           (imp (all set (lam y (imp (In y (pf:var 0)) (In y (pf:var 0)))))
                (In (pf:var 0) (Power (pf:var 0)))))
         (pf:imp-intro (pf:imp-intro (pf:hyp 0))))
       (pf:all-intro (pf:imp-intro (pf:hyp 0))))))
(law one power-self)
(law many power-self)
(law many universeIn)
(= (check-law $law)
   (hol:proves (pf:all-elim (pf:known $law) Empty) (In Empty (Power Empty))))
(= (from-space $which)
   (let $law (match &self (law $which $name) $name){$which}
     (hol:proves (pf:all-elim (pf:known $law) Empty) (In Empty (Power Empty)))))
(= (from-stream)
   (let $law (superpose (power-self universeIn)) (check-law $law)))
(= (nested-space $which)
   (let* (($law (match &self (law $which $name) $name){$which $name})
          ($answer (hol:proves (pf:all-elim (pf:known $law) Empty) (In Empty (Power Empty)))))
     (case $answer ((True accepted) (False rejected)))))
(= (case-space $which)
   (let $law (match &self (law $which $name) $name){$which}
     (case (hol:proves (pf:all-elim (pf:known $law) Empty) (In Empty (Power Empty)))
       ((True accepted) (False rejected)))))
(= (status-law $law)
   (let $result (try (hol:proves (pf:all-elim (pf:known $law) Empty) (In Empty (Power Empty))))
     (car-atom $result)))
(= (status-space $which)
   (let $law (match &self (law $which $name) $name){$which} (status-law $law)))
(= (data-proof $law) (pf:all-elim (pf:known $law) Empty))
(= (compute-proof $law)
   (let $proof (data-proof $law) (hol:proves $proof (In Empty (Power Empty)))))
!(check-law power-self)
!(check-law universeIn)
!(let $law power-self (check-law $law))
!(collapse (from-space one))
!(collapse (from-space many))
!(collapse (from-space missing))
!(collapse (from-stream))
!(collapse (nested-space many))
!(collapse (case-space many))
!(status-law power-self)
!(status-law universeIn)
!(status-law absent-law)
!(collapse (status-space many))
!(compute-proof power-self)
; An unevaluated program in a proof operand is not silently executed.
!(hol:proves (data-proof power-self) (In Empty (Power Empty)))
!(let $law (match &self (law one $name) $name) (uninterpreted-operation $law))

; Publication and dependent consumption inside a function are live effects,
; never constructor values cached by a pure-expression optimization.
(= (publish-from-space $name)
   (let $law (match &self (law one $source) $source)
     (hol:theorem $name (In Empty (Power Empty)) (pf:all-elim (pf:known $law) Empty))))
(= (consume-from-space $name)
   (let $found (match &self (hol:known $name $p $o $pf $d $r $s) $name){$name $p $pf}
     (let $package (hol:native-proof $found)
       (let (HolNativeProofV1 $n $p $pf $term $type $context $rules $assumptions $digest) $package
         (let (HolNativeUseV1 $checked $consumer $result (Id $carrier $left $right))
              (hol:native-use $package (Lam $type (Refl (idx 0))))
           (Consumed $n (== $checked $package) (== $left $term) (== $right $term)))){$p $pf})))
!(publish-from-space found-power)
!(consume-from-space found-power)
; An assumption under test: the fact proved from it follows its withdrawal.
!(hol:axiom temporary-law (In Empty (Power Empty)))
!(hol:theorem dependent-law (In Empty (Power Empty)) (pf:known temporary-law))
(= (current-law $name)
   (let $found (match &self (hol:known $name $p $o $pf $d $r $s) $name){$name}
     (let $status (try (hol:proves (pf:known $found) (In Empty (Power Empty))))
       (car-atom $status))))
!(current-law dependent-law)
!(match &self (hol:known temporary-law $p $o $pf $d $r $s)
   (remove-atom &self (hol:known temporary-law $p $o $pf $d $r $s)))
!(current-law dependent-law)
!(current-law found-power)
