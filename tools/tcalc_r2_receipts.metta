; Load after the R2 chainer and driver. A trace calls r2-t in place of t.
; Receipts capture the selected pre-state before the ordinary driver commits.
; Leaves are printed strings, so large native words remain exact opaque data.
; R2's store and proof constructors give each row id at most one row.
; A first read-only firing therefore realizes the named lookup. The
; ground-store law is LibTransform.fire_reads; the result stays a value.
(= (r2-read-occurrence $o)
   (case (tf:fire &kb ((read (occ $o $s $term))) ($s $term))
     (((tf:fired ($answer)) $answer) ($other $other))))
(= (r2-read-lemma $space $q)
   (case (tf:fire $space ((read (lres $q $term $s))) ($term $s))
     (((tf:fired ($answer)) $answer) ($other $other))))

(= (r2-code $term $vars)
   (if (is-var $term) (R2Local (r2-var-index $term $vars 0))
       (case $term
         (((R2VariableSlot $index) (R2Local $index))
          ($other (if (is-expr $other) (R2Expr (r2-code-list $other $vars))
                      (R2Value (repr $other))))))))
(= (r2-variable-slots $n $i)
   (if (== $i $n) ()
       (cons-atom (R2VariableSlot $i) (r2-variable-slots $n (+ $i 1)))))
; Open a separate rule copy and ground its entire frame at once. A bare
; schema variable must not be transported independently of that frame.
(= (r2-frame-code $r $target)
   (let (rule $id $name $holes $take $keep $tell $neq $absent $count $op $guard $fresh $inputs $birth $make) (rule-copy $r)
     (let* (($vars (firsts (cdr-atom $holes)))
            ($made-pattern (car-atom (cdr-atom (car-atom (cdr-atom $make)))))
            ($extra (if (== $target (none)) () (($made-pattern $target)))))
       (let $vars (r2-variable-slots (size-atom $vars) 0)
         ((r2-code (rule $id $name $holes $take $keep $tell $neq $absent $count $op $guard $fresh $inputs $birth $make) ())
          (r2-code $extra ()))))))
(= (r2-code-list $terms $vars)
   (if (== $terms ()) ()
       (cons-atom (r2-code (car-atom $terms) $vars)
                  (r2-code-list (cdr-atom $terms) $vars))))
(= (r2-var-index $var $vars $index)
   (if (== $vars ()) -1
       (if (== $var (car-atom $vars)) $index
           (r2-var-index $var (cdr-atom $vars) (+ $index 1)))))
(= (r2-selected $os)
   (if (== $os ()) ()
       (let $o (car-atom $os)
         (cons-atom ($o (r2-code (occ-fact $o) ()))
                    (r2-selected (cdr-atom $os))))))
(= (r2-query-rows $absent $count)
   (if (and (== $absent (absent)) (== $count (count))) ()
       (collapse (match &kb (occ $o $s $term) ($o (r2-code ($s $term) ()))))))
(= (r2-before-fire $r $takes $keeps $inst $mode)
   (case (rule-copy $r)
     ((Empty (R2Unavailable no-rule))
      ($rule
       (let (rule $id $name $holes $take $keep $tell $neq $absent $count $op $guard $fresh $inputs $birth $make) $rule
         (let ($rule-code $extra-code) (r2-frame-code $r (none))
           (R2Firing (R2Mode $mode (checks? absent $mode (cdr-atom $make))
                                 (checks? count $mode (cdr-atom $make)) (barred? $rule))
             $r $rule-code
             (append-atoms $takes $keeps)
             (r2-selected (append-atoms $takes $keeps))
             (r2-query-rows $absent $count) (r2-code $inst ()) $extra-code)))))))

; Promotion runs a rule at proof-local premises. These are separate from
; native store calls and receive a separate scope count in the C replay.
(= (r2-proof-id $q) (if (< $q 0) (+ 2 (* 2 (- -1 $q))) (+ 1 (* 2 $q))))
(= (r2-proof-ids $qs)
   (if (== $qs ()) () (cons-atom (r2-proof-id (car-atom $qs)) (r2-proof-ids (cdr-atom $qs)))))
(= (r2-proof-rows $ids $facts)
   (if (== $ids ()) ()
       (cons-atom ((car-atom $ids) (r2-code (car-atom $facts) ()))
                  (r2-proof-rows (cdr-atom $ids) (cdr-atom $facts)))))
(= (r2-before-lemma $rule $inst $prems $target $hyps $space)
   (let (rule $r $name $holes $take $keep $tell $neq $absent $count $op $guard $fresh $inputs $birth $make) $rule
     (case (lemma-facts $prems $hyps $space ())
       (((refused $why) (R2Unavailable $why))
        ($facts
         (let* (($ids (r2-proof-ids $prems))
                (($rule-code $extra-code) (r2-frame-code $r $target)))
           (R2Firing (R2Mode lemma false false false) $r $rule-code
             $ids (r2-proof-rows $ids $facts) () (r2-code $inst ()) $extra-code)))))))
(= (r2-lemma-match $rule $inst $prems $target $hyps $space)
   (let $before (r2-before-lemma $rule $inst $prems $target $hyps $space)
     (let $answer (lemma-match $rule $inst $prems $target $hyps $space)
       (let $_ (println! (R2Receipt (file-name) $before (R2Expr ((R2Value "lemma-step"))) (r2-code $answer ())))
         $answer))))
(= (r2-before $req)
   (case $req
     (((fire $r $takes $keeps $inst $dest)
        (r2-before-fire $r (occs-of $takes) (occs-of $keeps) $inst fire))
      ((commit $r $takes $keeps $inst $dest $seeds)
        (r2-before-fire $r (occs-of $takes) (occs-of $keeps) $inst (commit $seeds)))
      ((step $r $takes $keeps $inst $dest $checks $commit $seeds)
        (r2-before-fire $r (occs-of $takes) (occs-of $keeps) $inst (step $checks $commit $seeds)))
      ($other (R2Axis (car-atom $other))))))
(= (r2-t $req $answer)
   (let $before (r2-before $req)
     (let $result (t $req $answer)
       (let $_ (println! (R2Receipt (file-name) $before (r2-code $req ()) (r2-code $answer ())))
         $result))))
