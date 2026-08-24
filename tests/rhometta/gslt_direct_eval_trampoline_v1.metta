!(import! &self rhometta)

(= (gslt-rho:eval-trampoline-canary-v1)
   (rho:par
     (rho:send
       (rho:quote (rho:val gslt-rho:canary-channel-v1))
       (rhometta:eval (PairV1 LeftV1 RightV1)))
     (rho:recv
       (rho:quote (rho:val gslt-rho:canary-channel-v1))
       $__gslt_request
       (rho:par
         (rho:send
           (rho:quote
             (rho:par
               (rho:val gslt-rho:canary-eval-site-v1)
               (rho:drop $__gslt_request)))
           (rho:eval-payload
             (quote
               (let
                 (rho:quote (rho:val (PairV1 $__gslt_left RightV1)))
                 $__gslt_request
                 (rho:val (MatchedV1 $__gslt_left))))))
         (rho:recv
           (rho:quote
             (rho:par
               (rho:val gslt-rho:canary-eval-site-v1)
               (rho:drop $__gslt_request)))
           $__gslt_result
           (rho:drop $__gslt_result))))))

!(assertEqualToResult
   (rhometta:run (gslt-rho:eval-trampoline-canary-v1))
   ((rho:val (MatchedV1 LeftV1))))

!(assertEqualToResult
   (rhometta:run-canonical (gslt-rho:eval-trampoline-canary-v1))
   ((rho:val (MatchedV1 LeftV1))))
