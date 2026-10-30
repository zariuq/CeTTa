!(import! &self "curriculum.metta")
!(import! &self (library system))
; system:arg retains the compatibility CLI symbol representation. The store's
; byte-path interface takes a String, so convert that boundary explicitly.
(= (curriculum:argument $value)
   (case (get-metatype $value) ((Symbol (str:symbol->string $value)) ($other $value))))
!(if (system:has-args)
     (curriculum:run (curriculum:argument (system:arg 0)))
     (CurriculumUsage "Supply an existing private store directory."))
