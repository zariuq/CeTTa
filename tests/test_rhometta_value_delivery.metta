!(import! &self rhometta)

; Payload evaluation has finished before the native frontier is delivered.
; These fields are values, even when their syntax names an evaluator operation.
(= (delivery:effect) (add-atom &self delivery-was-executed))
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
