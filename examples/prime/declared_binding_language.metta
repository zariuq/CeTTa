; A language package declares its scopes and its interpretation separately.
; Local identities, free dependencies and the original syntax remain
; inspectable. The authored GSLT rules build ordinary Prime syntax as data.
!(import! &self "../../langdef/prime/declared_binding_package_v1.metta")

!(let $program (binding:prepare
       @(binding:lambda $z (binding:where $y $z (binding:result (g $y)))))
   (let $f (binding:function $program) (Pair ($f 1) ($f 2))))

; Runtime-built source follows the same declared scopes.
!(let $source (cons-atom quote
       ((binding:lambda $z (binding:where $y $z (binding:result (g $y))))))
   (let $program (binding:prepare $source)
     (let $f (binding:function $program) (Pair ($f 1) ($f 2)))))

; An explicitly shared dependency remains connected to the caller.
!(let $program (binding:prepare
       @(binding:lambda $z (binding:result (Pair $z $shared))){$shared})
   (let $f (binding:function $program)
     (unify $shared 7 (Pair ($f 1) ($f 2)) failed)))

; The value of an inner binding is outside that binding's scope.
!(let $program (binding:prepare
       @(binding:lambda $z
          (binding:where $y $z
            (binding:where $y $y (binding:result (g $y))))))
   (let $f (binding:function $program) (Pair ($f 1) ($f 2))))

; The scope package and lowering source survive alongside the function.
!(let $program (binding:prepare
       @(binding:lambda $z (binding:where $y $z (binding:result (g $y)))))
   (let $instance (langdef:lowering-source $program)
     (let (quote $receipts) (langdef:instance-bindings $instance)
       (- (size-atom $receipts) 1))))

; The same scopes and GSLT rules can use a declared nominal name class.
; That fragment reaches Prime's named dependent checker directly.
!(let $signature (binding:signature-with (AbtNames (SymbolPrefix "?")))
   (let $program (binding:prepare-with $signature
         @(binding:lambda ?A
            (binding:lambda ?x (binding:result (refl ?x)))))
     (let $f (binding:function $program)
       (type:check $f (-> (A : (u 0)) (x : A) (id A x x))))))

; Replacing the witness by its type does not prove that witness equal to itself.
!(let $signature (binding:signature-with (AbtNames (SymbolPrefix "?")))
   (let $program (binding:prepare-with $signature
         @(binding:lambda ?A
            (binding:lambda ?x (binding:result (refl ?A)))))
     (let $f (binding:function $program)
       (type:check $f (-> (A : (u 0)) (x : A) (id A x x))))))
