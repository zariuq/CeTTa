; A contextual checking adapter for the declared single-name fragment.
; The source instance, original private identities and occurrence receipts
; remain in the result. Scope resolution and authored interpretation are
; explicit operations; importing the package changes no language defaults.
!(import! &self langdef)
!(bind! &binding-dependent-lowering
   (langdef:load-deterministic-equations
     (LangDef:EquationSources "langdef/prime/declared_binding_dependent_v1.metta")))

(= (binding:dependent-code $context
      (LangDef:BindingInstance $signature $source (quote $term) $receipts))
   (let (quote $resolved) (langdef:resolve-context $signature $context $term)
     (let $plan &binding-dependent-lowering
       (let $lowered (langdef:run-deterministic-equations-data
           $plan
           (LangDef:DeterministicCall binding:dependent-lower $resolved))
         (langdef:quote-syntax $lowered)))))

(= (binding:dependent-instance $context $instance)
   (let $code (binding:dependent-code $context $instance)
     (LangDef:DependentBindingInstance $context $instance $code)))

(= (binding:dependent-term
      (LangDef:DependentBindingInstance $context $instance (quote $term)))
   $term)

(= (binding:dependent-check $context $instance (quote $type))
   (let $packet (binding:dependent-instance $context $instance)
     (let (LangDef:DependentBindingInstance $ambient $source (quote $term)) $packet
       (type:check $term $type))))

; Abstract an actual open source instance, retaining it alongside the new
; instance. The scope reader freshens the complete abstraction as one unit.
(= (binding:abstract-instance $name
      (LangDef:BindingInstance $signature $source (quote $term) $receipts))
   (let $code (langdef:quote-syntax (binding:lambda $name $term))
     (let $closed (langdef:instantiate-code $signature () $code)
       (LangDef:ContextAbstraction $name
         (LangDef:BindingInstance $signature $source (quote $term) $receipts)
         $closed))))

(= (binding:abstracted-source (LangDef:ContextAbstraction $name $open $closed)) $closed)
