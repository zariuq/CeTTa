; A declared nominal name class connects the same authored lowering to the
; existing named-term checker. Nominal freshness is relative to the code;
; private logical-variable identities are a separate representation.
!(import! &self "../../../langdef/prime/declared_binding_package_v1.metta")
(= (nominal:prepare $code)
   (let $signature (binding:signature-with (AbtNames (SymbolPrefix "?")))
     (binding:prepare-with $signature $code)))

!(let $program (nominal:prepare @(binding:lambda ?z (binding:result ?z)))
   (let $f (binding:function $program) (type:check $f (-> u0 u0))))
!(let $program (nominal:prepare @(binding:lambda ?z (binding:result ?z)))
   (let $f (binding:function $program)
     (type:check $f (-> u0 (-> u0 u0)))))

!(let $program (nominal:prepare
       @(binding:lambda ?z (binding:result (refl ?z))))
   (let $f (binding:function $program)
     (type:check $f (-> (x : u0) (id u0 x x)))))
!(let $program (nominal:prepare
       @(binding:lambda ?z (binding:result (refl u0))))
   (let $f (binding:function $program)
     (type:check $f (-> (x : u0) (id u0 x x)))))

; Dependent checking retains both the type argument and its exact witness.
!(let $program (nominal:prepare
       @(binding:lambda ?A
          (binding:lambda ?x (binding:result (refl ?x)))))
   (let $f (binding:function $program)
     (type:check $f
       (-> (A : (u 0)) (x : A) (id A x x)))))
!(let $program (nominal:prepare
       @(binding:lambda ?A
          (binding:lambda ?x (binding:result (refl ?A)))))
   (let $f (binding:function $program)
     (type:check $f
       (-> (A : (u 0)) (x : A) (id A x x)))))

; Merely changing the name class does not make private variable syntax an
; admitted named proof term. The explicit dependent-code adapter supplies
; the contextual interpretation without changing the named-term interface.
!(let $program (binding:prepare
       @(binding:lambda $z (binding:result (refl $z))))
   (let $f (binding:function $program)
     (type:check $f (-> (x : u0) (id u0 x x)))))
