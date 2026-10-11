; Scope declarations and an authored interpreter are separate package data.
!(import! &self langdef)

(= (binding:signature-with $names)
   (AbtSignatures $names
     (AbtSignature binding:lambda (Fields parameter body)
       (BindPattern parameter (Scope body)))
     (AbtSignature binding:where (Fields pattern source body)
       (BindPattern pattern (Scope body)))))

(= (binding:signature)
   (binding:signature-with (AbtNames Variables)))

!(bind! &binding-lowering
   (langdef:load-deterministic-equations
     (LangDef:EquationSources "langdef/prime/declared_binding_lowering_v1.metta")))

(= (binding:prepare-with $signature $code)
   (let $instance (langdef:instantiate-code $signature () $code)
     (let $plan &binding-lowering
       (langdef:lower-instance $plan binding:lower $instance))))

(= (binding:prepare $code)
   (let $signature (binding:signature)
     (binding:prepare-with $signature $code)))

; Formation and execution are separate. The lowering's syntax is admitted
; to Prime's binding elaborator explicitly; using its result executes it.
(= (binding:function $program)
   (let (quote $syntax) (langdef:lowered-code $program)
     (let $code (prime:elaborate $syntax) (unquote $code))))
