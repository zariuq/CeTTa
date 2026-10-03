!(import! &self rhometta)

; Payload evaluation has finished before the native frontier is delivered.
; These fields are values, even when their syntax names an evaluator operation.
(= (delivery:effect) (add-atom &self delivery-was-executed))
(: delivery:payload (-> Atom))
(= (delivery:payload)
   (function (return (Bundle (Error sentinel detail) (empty)
     (+ 4 5) (delivery:effect)))))
(= (delivery:program)
   (rho:par
     (rho:send (rho:quote rho:nil) (rhometta:eval (delivery:payload)))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

!(assertEqualToResult (rhometta:transitions (delivery:program))
   ((rho:val (Bundle (Error sentinel detail) (empty)
     (+ 4 5) (delivery:effect)))))
!(assertEqualToResult (rhometta:run (delivery:program))
   ((rho:val (Bundle (Error sentinel detail) (empty)
     (+ 4 5) (delivery:effect)))))
!(assertEqualToResult (rhometta:run-canonical (delivery:program))
   ((rho:val (Bundle (Error sentinel detail) (empty)
     (+ 4 5) (delivery:effect)))))

; Value observation preserves occurrences and does not evaluate their fields.
!(assertEqualToResult
   (rhometta:values (function (return (rho:par
     (rho:val (Error sentinel detail))
     (rho:val (empty))
     (rho:val (+ 4 5))
     (rho:val (+ 4 5))
     (rho:val (delivery:effect))))))
   (((Error sentinel detail) (empty) (+ 4 5) (+ 4 5)
     (delivery:effect))))
!(assertEqualToResult (collapse (match &self delivery-was-executed found)) (()))

; Ordinary MeTTa constructor fields still evaluate and propagate failures.
!(assertEqualToResult (Box (+ 4 5)) ((Box 9)))
!(assertEqualToResult (Box (+ 1 "S"))
   ((Error (+ 1 "S") (BadArgType 2 Number String))))
!(assertEqualToResult (Box (empty)) ())

; Matching diagnostic text is still ordinary data unless export actually fails.
!(assertEqualToResult
   (rhometta:run (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval (function (return
         (Error rhometta:eval PayloadOwnedExportAliased)))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))
   ((rho:val (Error rhometta:eval PayloadOwnedExportAliased))))
!(assertEqualToResult (rhometta:values (rhometta:run (delivery:program)))
   (((Bundle (Error sentinel detail) (empty) (+ 4 5) (delivery:effect)))))
!(assertEqualToResult (collapse (rhometta:transitions rho:nil)) (()))

; A completed native value can contain a function-shaped inert payload.
(: delivery:function-state (-> Atom))
(= (delivery:function-state)
   (function (return (rho:val (function (return (+ 1 2)))))))
!(assertEqualToResult
   (__cetta_lib_rhometta_run_canonical (delivery:function-state))
   ((rho:val (function (return (+ 1 2))))))
!(assertEqualToResult
   (rhometta:values (rhometta:run-canonical (delivery:function-state)))
   (((function (return (+ 1 2))))))

; Inert publication preserves data but still enforces concrete type demands.
; These declarations follow the earlier observations so they cannot alter them.
(: rho:nil Foo)
!(assertEqualToResult
   (metta (__cetta_lib_rhometta_run_canonical rho:nil) Foo &self)
   (rho:nil))
!(assertEqualToResult
   (metta (__cetta_lib_rhometta_run_canonical rho:nil) Number &self)
   ((Error rho:nil (BadType Number Foo))))
!(assertEqualToResult
   (function
      (chain (metta (__cetta_lib_rhometta_run_canonical rho:nil) $type &self)
         $value (return ($value $type))))
   ((rho:nil Foo)))

(: __cetta_lib_rhometta_run_canonical (-> Atom Number))
!(assertEqualToResult (__cetta_lib_rhometta_run_canonical rho:nil)
   ((Error rho:nil (BadType Number Foo))))

; Atom-only continuations retain native provenance for every equation answer.
(= (delivery:multiple-native-state)
   (__cetta_lib_rhometta_values (function (return (rho:val (+ 1 2))))))
(= (delivery:multiple-native-state)
   (__cetta_lib_rhometta_values (function (return (rho:val (+ 1 2))))))
!(assertEqualToResult (delivery:multiple-native-state)
   (((+ 1 2)) ((+ 1 2))))
(= (delivery:multiple-native-frontier)
   (__cetta_lib_rhometta_run rho:nil))
(= (delivery:multiple-native-frontier)
   (__cetta_lib_rhometta_run rho:nil))
!(assertEqualToResult (delivery:multiple-native-frontier)
   (rho:nil rho:nil))
