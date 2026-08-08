!(import! &self (library lib_import))

(= (compiled-before-import)
   (libpl_phase_value))

!(import_prolog_functions_from_file
    "tests/petta/libpl_call_plan_fixture.pl"
    (libpl_phase_value
     libpl_opaque_empty libpl_opaque_size
     libpl_opaque_head libpl_opaque_tail libpl_opaque_cons))

(= (compiled-after-import)
   (libpl_phase_value))

(= (opaque-copy $statement)
   (if (== (libpl_opaque_size $statement) 0)
       (libpl_opaque_empty)
       (libpl_opaque_cons
         (libpl_opaque_head $statement)
         (opaque-copy (libpl_opaque_tail $statement)))))

!(println! (phase-before (compiled-before-import)))
!(println! (phase-after (compiled-after-import)))
!(let $statement
      (libpl_opaque_cons term
        (libpl_opaque_cons 0 (libpl_opaque_empty)))
   (println! (opaque-copy-two (opaque-copy $statement))))
