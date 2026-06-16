!(import! &self rhometta)

; Positive example:
; threaded Rhometta payloads may allocate local spaces/states and then drop
; them without changing the shared base or tripping worker-teardown cleanup.
;
; Negative example guarded against by this witness:
; a worker-local transient handle used to be exactly the kind of thing that
; could survive into later cleanup bookkeeping and explode only after the COMM
; itself looked successful.

(= (scratch:discard-space-once)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $holder (new-space)
           (let $sub (new-space)
             (let $_ (add-atom $sub scratch)
               (let $_ (add-atom $holder (nested-space $sub))
                 ok))))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (scratch:discard-state-once)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $st (new-state 0)
           (let $_ (change-state! $st 1)
             ok))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))

!(pragma! num-threads 2)

!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-space-once)) ((rho:val ok)))
!(assertEqualToResult (rhometta:run (scratch:discard-state-once)) ((rho:val ok)))
