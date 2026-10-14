; All generic scope operations consume the same language declarations.
!(import! &self "../../../langdef/prime/declared_binding_package_v1.metta")
(= (observe $label $value) (Observed $label $value))

!(observe written
   (let $program (binding:prepare
         @(binding:lambda $z (binding:where $y $z (binding:result (g $y)))))
     (let $f (binding:function $program) (Pair ($f 1) ($f 2)))))
!(observe built
   (let $source (cons-atom quote
         ((binding:lambda $z (binding:where $y $z (binding:result (g $y))))))
     (let $program (binding:prepare $source)
       (let $f (binding:function $program) (Pair ($f 1) ($f 2))))))
!(observe nested-value
   (let $program (binding:prepare
         @(binding:lambda $z
            (binding:where $y $z
              (binding:where $y $y (binding:result (g $y))))))
     (let $f (binding:function $program) (Pair ($f 1) ($f 2)))))
!(observe live
   (let $program (binding:prepare
         @(binding:lambda $z (binding:result (Pair $z $shared))){$shared})
     (let $f (binding:function $program)
       (unify $shared 7 (Pair ($f 1) ($f 2)) failed))))
!(observe built-live
   (let $code (cons-atom quote
         ((binding:lambda $z (binding:result (Pair $z $shared)))))
     (let $program (binding:prepare $code)
       (let $f (binding:function $program)
         (unify $shared 7 (Pair ($f 1) ($f 2)) failed)))))
!(observe retained-bindings
   (let $program (binding:prepare
         @(binding:lambda $z (binding:where $y $z (binding:result (g $y)))))
     (let $instance (langdef:lowering-source $program)
       (let (quote $receipts) (langdef:instance-bindings $instance)
         (- (size-atom $receipts) 1)))))
!(observe independent-instances
   (let $signature (binding:signature)
     (let $code @(binding:lambda $z (binding:result (g $z)))
       (let $a (langdef:instantiate-code $signature () $code)
         (let $b (langdef:instantiate-code $signature () $code)
           (let (quote $left) (langdef:instance-code $a)
             (let (quote $right) (langdef:instance-code $b)
               (Pair (== $left $right)
                     (langdef:alpha-equal $signature $left $right)))))))))
!(observe renamed-locals
   (let $signature (binding:signature)
     (let (quote $left)
          @(binding:lambda $z (binding:where $y $z (binding:result (g $y))))
       (let (quote $right)
            @(binding:lambda $p (binding:where $q $p (binding:result (g $q))))
         (langdef:alpha-equal $signature $left $right)))))
!(observe changed-free-name
   (let $signature (binding:signature)
     (let (quote $left) @(binding:lambda $z (binding:result (Pair $z $x))){$x}
       (let (quote $right) @(binding:lambda $z (binding:result (Pair $z $y))){$y}
         (langdef:alpha-equal $signature $left $right)))))
!(observe free-context
   (let $signature (binding:signature)
     (let $instance (langdef:instantiate-code $signature ()
           @(binding:lambda $z (binding:result (Pair $z $shared))){$shared})
       (let (quote $term) (langdef:instance-code $instance)
         (== (langdef:free-names $signature $term) ($shared))))))
!(observe capture-avoiding-substitution
   (let $signature (binding:signature)
     (let (quote $source)
          @(binding:lambda $z (binding:result (Pair $z $shared))){$z $shared}
       (let $term (langdef:substitute-names $signature (($shared $z)) $source)
         (Pair (== (langdef:free-names $signature $term) ($z))
               (langdef:alpha-equal $signature $term
                 (binding:lambda $z (binding:result (Pair $z $z)))))))))
!(__cetta_abt_instantiate
   (AbtSignatures (AbtNames Variables)
     (AbtSignature Bad (Fields p b) (BindPattern missing (Scope b))))
   () (Bad $x $x))
!(let $signature (binding:signature)
   (langdef:instantiate-names $signature 7
     (binding:lambda $x (binding:result $x))))
!(let $signature (binding:signature)
   (langdef:instantiate-names $signature () (binding:lambda $x)))
; Formation leaves uninterpreted atoms intact and does not run their code.
!(observe inert (prime:elaborate (Unknown 7)))
!(observe formed-twice
   (let $program (binding:prepare
         @(binding:lambda $z (binding:where $y $z (binding:result (g $y)))))
     (let $f (binding:function $program)
       (let $code (prime:elaborate $f)
         (let $again (unquote $code)
           (Pair (== $f $again) (Pair ($again 1) ($again 2))))))))
