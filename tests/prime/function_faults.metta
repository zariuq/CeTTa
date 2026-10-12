; A function delimiter preserves completed faults, rather than disguising
; them as an absent return. Successful bodies still require an explicit return.
!(assertEqualToResult
   (function (case (Error probe "failure") (($r (return $r)))))
   ((Error probe "failure")))
!(assertEqualToResult
   (function (== 1 "bad"))
   ((Error (== 1 "bad") (BadArgType 2 Number String))))
!(assertEqualToResult
   (function (function (case (Error probe "failure") (($r (return $r))))))
   ((Error probe "failure")))
!(assertEqualToResult
   (function (return (Error probe "returned failure")))
   ((Error probe "returned failure")))
!(assertEqualToResult (function (return 42)) (42))
!(assertEqualToResult
   (function 42)
   ((Error (function 42) NoReturn)))
!(assertEqualToResult
   (function (superpose ((Error probe "failure") (Error probe "failure"))))
   ((Error probe "failure") (Error probe "failure")))
!(assertEqualToResult
   (function (chain (superpose (1 2)) $x (return $x)))
   (1 2))
; Caught faults retain the branch's memoized result across the delimiter.
!(assertEqualToResult
   (let $p (delay (function (== 1 "bad")))
     (Pair (if-error (force $p) caught missed)
           (if-error (force $p) caught missed)))
   ((Pair caught caught)))
!(assertEqualToResult
   (function (superpose ()))
   ((Error (function (superpose ())) NoReturn)))
; A faulting producer runs once in the memoized branch; its update is not
; committed to the ambient world after that branch finishes.
!(bind! &function-faults:counter (new-state 0))
(= (function-faults:fail-after-effect)
   (let $old (get-state &function-faults:counter)
     (let $_ (change-state! &function-faults:counter (+ $old 1))
       (== 1 "bad"))))
!(assertEqualToResult
   (let $p (delay (function (function-faults:fail-after-effect)))
     (let $_first (if-error (force $p) caught missed)
       (let $_second (if-error (force $p) caught missed)
         (get-state &function-faults:counter))))
   (1))
!(assertEqualToResult (get-state &function-faults:counter) (0))
