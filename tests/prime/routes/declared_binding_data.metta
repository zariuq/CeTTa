; Returning, retaining, inspecting and forming syntax must not execute it.
!(import! &self "../../../langdef/prime/declared_binding_package_v1.metta")
!(langdef:quote-syntax (println! "unrequested-direct-execution"))
!(prime:elaborate (println! "unrequested-direct-formation"))
!(let $signature (binding:signature)
   (let $instance (langdef:instantiate-code $signature ()
         @(binding:result (println! "unrequested-instance-execution")))
     (langdef:instance-code $instance)))
!(let $program (binding:prepare
       @(binding:result (println! "unrequested-lowering-execution")))
   (langdef:lowered-code $program))
!(let $program (binding:prepare
       @(binding:result (println! "unrequested-formation-execution")))
   (let (quote $syntax) (langdef:lowered-code $program)
     (let $formed-code (prime:elaborate $syntax) (size-atom $formed-code))))
!(let $program (binding:prepare
       @(binding:result (println! "unrequested-receipt-execution")))
   (let $instance (langdef:lowering-source $program)
     (langdef:instance-bindings $instance)))
; Opening is a separate request to execute the retained program.
!(let $program (binding:prepare
       @(binding:result (println! "intentional-execution")))
   (let $code (langdef:lowered-code $program) (unquote $code)))
