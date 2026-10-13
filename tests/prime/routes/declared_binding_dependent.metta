; The explicit contextual bridge retains scope, source instances and indices.
!(import! &self "../../../langdef/prime/declared_binding_package_v1.metta")
!(import! &self "../../../langdef/prime/declared_binding_dependent_package_v1.metta")
(= (observed $label $value) (Observed $label $value))

!(observed private-identity
   (let $program (binding:prepare @(binding:lambda $z (binding:result (refl $z))))
     (let $instance (langdef:lowering-source $program)
       (binding:dependent-check () $instance @(-> (x : u0) (id u0 x x))))))
!(observed wrong-index
   (let $program (binding:prepare @(binding:lambda $z (binding:result (refl u0))))
     (let $instance (langdef:lowering-source $program)
       (binding:dependent-check () $instance @(-> (x : u0) (id u0 x x))))))
!(observed varying-carrier
   (let $program (binding:prepare
         @(binding:lambda $A (binding:lambda $x (binding:result (refl $x)))))
     (let $instance (langdef:lowering-source $program)
       (binding:dependent-check () $instance @(-> (A : (u 0)) (x : A) (id A x x))))))
!(observed wrong-carrier
   (let $program (binding:prepare
         @(binding:lambda $A (binding:lambda $x (binding:result (refl $A)))))
     (let $instance (langdef:lowering-source $program)
       (binding:dependent-check () $instance @(-> (A : (u 0)) (x : A) (id A x x))))))
!(observed where-value-outside
   (let $program (binding:prepare
         @(binding:lambda $z (binding:where $z $z (binding:result (refl $z)))))
     (let $instance (langdef:lowering-source $program)
       (binding:dependent-check () $instance @(-> (x : u0) (id u0 x x))))))
!(observed constructed
   (let $code (cons-atom quote
         ((binding:lambda $z (binding:where $y $z (binding:result (refl $y))))))
     (let $program (binding:prepare $code)
       (let $instance (langdef:lowering-source $program)
         (binding:dependent-check () $instance @(-> (x : u0) (id u0 x x)))))))

!(observed context-order
   (let $signature (binding:signature)
     (let $first (langdef:resolve-context $signature ($a $b)
           (binding:lambda $x (binding:result (Pair $x $a $b))))
       (let $second (langdef:resolve-context $signature ($b $a)
             (binding:lambda $x (binding:result (Pair $x $a $b))))
         (Pair
           (== $first @(binding:lambda (idx 0)
             (binding:result (Pair (idx 0) (idx 1) (idx 2)))))
           (== $second @(binding:lambda (idx 0)
             (binding:result (Pair (idx 0) (idx 2) (idx 1))))))))))
!(observed value-context
   (let $signature (binding:signature)
     (let $actual (langdef:resolve-context $signature ($base)
           (binding:lambda $x (binding:where $y $base
             (binding:result (Pair $x $y $base)))))
       (== $actual @(binding:lambda (idx 0)
         (binding:where (idx 0) (idx 1)
           (binding:result (Pair (idx 1) (idx 0) (idx 2)))))))))

!(observed abstract-open-instance
   (let $signature (binding:signature)
     (let $open (langdef:instantiate-code $signature ()
           @(binding:lambda $x (binding:result (refl $base))){$base})
       (let $closure (binding:abstract-instance $base $open)
         (let $closed (binding:abstracted-source $closure)
           (binding:dependent-check () $closed
             @(-> (b : u0) (x : u0) (id u0 b b))))))))
!(observed capture-fails-contract
   (let $signature (binding:signature)
     (let $open (langdef:instantiate-code $signature ()
           @(binding:lambda $x (binding:result (refl $x))))
       (let $closure (binding:abstract-instance $base $open)
         (let $closed (binding:abstracted-source $closure)
           (binding:dependent-check () $closed
             @(-> (b : u0) (x : u0) (id u0 b b))))))))

!(observed fresh-instances
   (let $signature (binding:signature)
     (let $left (langdef:instantiate-code $signature ()
           @(binding:lambda $x (binding:result (refl $x))))
       (let $right (langdef:instantiate-code $signature ()
             @(binding:lambda $renamed (binding:result (refl $renamed))))
         (== (binding:dependent-code () $left) (binding:dependent-code () $right))))))
; Data interpretation must not execute an unknown payload.
!(let $signature (binding:signature)
   (let $instance (langdef:instantiate-code $signature ()
         @(binding:result (println! "unrequested-contextual-execution")))
     (binding:dependent-code () $instance)))
