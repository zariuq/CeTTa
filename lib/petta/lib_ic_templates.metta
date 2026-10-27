; Ranked templates use the existing canonical-schema representation:
; (IC:Node operator-index children), (IC:Hole role index).
; Signature rows are (IC:Operator result-sort ((IC:Slot role sort) ...)).
; The operator index is its position in that ordered signature. These values
; are syntax, not executable calls. Interpreting bound-variable holes requires
; the scoped adapter; an encoded binder record alone grants no such permission.
!(import! &self (library binary))

(= (ic-template:natural? $n)
   (binary:unsigned? $n))

(= (ic-template:role? $role)
   (or (== $role Root) (or (== $role Code) (== $role Value))))

(= (ic-template:slots? $slots)
   (if (== $slots ()) True
     (if (== (get-metatype $slots) Expression)
       (let ($slot $tail) (decons-atom $slots)
         (case $slot
           (((IC:Slot $role $sort)
             (and (ic-template:role? $role)
               (and (== (get-metatype $sort) Symbol) (ic-template:slots? $tail))))
            ($other False))))
       False)))

(= (ic-template:signature? $signature)
   (if (== $signature ()) True
     (if (== (get-metatype $signature) Expression)
       (let ($row $tail) (decons-atom $signature)
         (case $row
           (((IC:Operator $sort $slots)
             (and (== (get-metatype $sort) Symbol)
               (and (ic-template:slots? $slots) (ic-template:signature? $tail))))
            ($other False))))
       False)))

; Called only after validating an exact nonnegative machine integer.
(= (ic-template:operator $signature $index)
   (if (== $signature ()) IC:None
     (let ($row $tail) (decons-atom $signature)
       (if (== $index 0) (IC:Some $row)
         (ic-template:operator $tail (- $index 1))))))

(= (ic-template:lookup $environment $role $index)
   (if (== $environment ()) IC:None
     (let ($binding $tail) (decons-atom $environment)
       (case $binding
         (((IC:Binding $old-role $old-index $value)
           (if (and (== $role $old-role) (== $index $old-index))
             (IC:Some $value) (ic-template:lookup $tail $role $index))))))))

(= (ic-template:assign $environment $role $index $value)
   (case (ic-template:lookup $environment $role $index)
     (((IC:Some $old) (if (== $old $value) (IC:Some $environment) IC:None))
      (IC:None (let $next (cons-atom (IC:Binding $role $index $value) $environment)
                 (IC:Some $next))))))

; Admission threads the hole sorts, independently of matching source values.
(= (ic-template:admit $signature $role $sort $pattern $holes)
   (case $pattern
     (((IC:Hole $actual $index)
       (if (and (== $actual $role) (ic-template:natural? $index))
         (ic-template:assign $holes $role $index $sort) IC:None))
      ((IC:Node $index $children)
       (if (ic-template:natural? $index)
         (case (ic-template:operator $signature $index)
           (((IC:Some (IC:Operator $actual $slots))
             (if (== $actual $sort)
               (ic-template:admit-list $signature $slots $children $holes) IC:None))
            ($other IC:None))) IC:None))
      ($other IC:None))))

(= (ic-template:admit-list $signature $slots $patterns $holes)
   (if (== $slots ()) (if (== $patterns ()) (IC:Some $holes) IC:None)
     (if (and (== (get-metatype $patterns) Expression) (not (== $patterns ())))
       (let* ((($slot $slots-tail) (decons-atom $slots))
              (($pattern $patterns-tail) (decons-atom $patterns)))
         (case $slot
           (((IC:Slot $role $sort)
             (case (ic-template:admit $signature $role $sort $pattern $holes)
               (((IC:Some $next)
                 (ic-template:admit-list $signature $slots-tail $patterns-tail $next))
                ($other IC:None)))))))
       IC:None)))

(= (ic-template:source? $signature $sort $source)
   (case $source
     (((IC:Node $index $children)
       (if (ic-template:natural? $index)
         (case (ic-template:operator $signature $index)
           (((IC:Some (IC:Operator $actual $slots))
             (and (== $actual $sort) (ic-template:sources? $signature $slots $children)))
            ($other False))) False))
      ($other False))))

(= (ic-template:sources? $signature $slots $sources)
   (if (== $slots ()) (== $sources ())
     (if (and (== (get-metatype $sources) Expression) (not (== $sources ())))
       (let* ((($slot $slots-tail) (decons-atom $slots))
              (($source $sources-tail) (decons-atom $sources)))
         (case $slot
           (((IC:Slot $role $sort)
             (and (ic-template:source? $signature $sort $source)
               (ic-template:sources? $signature $slots-tail $sources-tail))))))
       False)))

; This worker receives admitted syntax. It never invokes a source operator.
(= (ic-template:match-into $environment $pattern $source)
   (case $pattern
     (((IC:Hole $role $index) (ic-template:assign $environment $role $index $source))
      ((IC:Node $index $children)
       (case $source
         (((IC:Node $actual $values)
           (if (== $index $actual)
             (ic-template:match-list $environment $children $values) IC:None))))))))

(= (ic-template:match-list $environment $patterns $sources)
   (if (== $patterns ()) (if (== $sources ()) (IC:Some $environment) IC:None)
     (if (== $sources ()) IC:None
       (let* ((($pattern $tail) (decons-atom $patterns))
              (($source $remaining) (decons-atom $sources)))
         (case (ic-template:match-into $environment $pattern $source)
           (((IC:Some $next) (ic-template:match-list $next $tail $remaining))
            ($other IC:None)))))))

(= (ic-template:expand $environment $pattern)
   (case $pattern
     (((IC:Hole $role $index) (ic-template:lookup $environment $role $index))
      ((IC:Node $index $children)
       (case (ic-template:expand-list $environment $children)
         (((IC:Some $values) (IC:Some (IC:Node $index $values)))
          ($other IC:None))))
      ($other IC:None))))

(= (ic-template:expand-list $environment $patterns)
   (if (== $patterns ()) (IC:Some ())
     (let ($pattern $tail) (decons-atom $patterns)
       (case (ic-template:expand $environment $pattern)
         (((IC:Some $source)
           (case (ic-template:expand-list $environment $tail)
             (((IC:Some $sources)
               (let $values (cons-atom $source $sources) (IC:Some $values)))
              ($other IC:None))))
          ($other IC:None))))))

; A forest shares an environment: use this for a rewrite's left and right
; sides. Separate corpus occurrences each start with an empty environment.
(= (ic-template:match $signature $slots $patterns $sources)
   (if (and (ic-template:signature? $signature) (ic-template:slots? $slots))
     (case (ic-template:admit-list $signature $slots $patterns ())
       (((IC:Some $holes)
         (if (ic-template:sources? $signature $slots $sources)
           (ic-template:match-list () $patterns $sources) (IC:Invalid Source)))
        ($other (IC:Invalid Template))))
     (IC:Invalid Signature)))

; Pairwise seeds use first occurrence coordinates in the hole stream, not
; counters of distinct holes. Repeated disagreements retain the first index.
(= (ic-template:seed-lookup $memo $role $left $right)
   (if (== $memo ()) IC:None
     (let ($key $tail) (decons-atom $memo)
       (case $key
         (((IC:Key $old-role $old-left $old-right $index)
           (if (and (== $old-role $role)
                    (and (== $old-left $left) (== $old-right $right)))
             (IC:Some $index) (ic-template:seed-lookup $tail $role $left $right))))))))

(= (ic-template:seed-hole $role $left $right $state)
   (case $state
     (((IC:SeedState $position $memo)
       (let $index (case (ic-template:seed-lookup $memo $role $left $right)
                    (((IC:Some $old) $old) (IC:None $position)))
         (let $next (cons-atom (IC:Key $role $left $right $index) $memo)
           (IC:Seed (IC:Hole $role $index) (IC:SeedState (+ $position 1) $next))))))))

(= (ic-template:seed $signature $role $left $right $state)
   (case ($left $right)
     ((((IC:Node $left-op $left-children) (IC:Node $right-op $right-children))
       (if (== $left-op $right-op)
         (case (ic-template:operator $signature $left-op)
           (((IC:Some (IC:Operator $sort $slots))
             (case (ic-template:seed-list $signature $slots $left-children $right-children $state)
               (((IC:Seed $patterns $next) (IC:Seed (IC:Node $left-op $patterns) $next)))))))
         (ic-template:seed-hole $role $left $right $state))))))

(= (ic-template:seed-list $signature $slots $lefts $rights $state)
   (if (== $slots ()) (IC:Seed () $state)
     (let* ((($slot $remaining) (decons-atom $slots))
            (($left $left-tail) (decons-atom $lefts))
            (($right $right-tail) (decons-atom $rights)))
       (case $slot
         (((IC:Slot $role $sort)
           (case (ic-template:seed $signature $role $left $right $state)
             (((IC:Seed $pattern $middle)
               (case (ic-template:seed-list $signature $remaining $left-tail $right-tail $middle)
                 (((IC:Seed $tail $next)
                   (let $patterns (cons-atom $pattern $tail) (IC:Seed $patterns $next))))))))))))))

(= (ic-template:generalize $signature $slots $lefts $rights)
   (if (and (ic-template:signature? $signature) (ic-template:slots? $slots))
     (if (and (ic-template:sources? $signature $slots $lefts)
              (ic-template:sources? $signature $slots $rights))
       (case (ic-template:seed-list $signature $slots $lefts $rights (IC:SeedState 0 ()))
         (((IC:Seed $patterns $state)
           (let* (($left-env (ic-template:match $signature $slots $patterns $lefts))
                  ($right-env (ic-template:match $signature $slots $patterns $rights)))
             (case ($left-env $right-env)
               ((((IC:Some $ls) (IC:Some $rs)) (IC:Generalized $patterns $ls $rs))
                ($other (IC:Invalid GeneratedTemplate))))))))
       (IC:Invalid Source))
     (IC:Invalid Signature)))
