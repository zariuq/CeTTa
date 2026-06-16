!(import! &self rhometta)

; Positive example:
; a worker-created new-space may survive a threaded Rhometta payload only as an
; owned promoted/cloned result, including nested child spaces and a handle
; routed through payload-local bind!.
;
; Negative example guarded against by this witness:
; any surviving handle that still points at worker-local scratch or deferred
; worker-teardown bookkeeping instead of the promoted/cloned survivor.

(= (unwrap-rho-val (rho:val $x)) $x)

(= (affine:chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (affine:chan (- $n 1)) rho:nil))))

(= (affine:owned-space-once)
   (rho:par
     (rho:send (affine:chan 1)
       (rhometta:eval
         (let $s (new-space)
           (let $_ (add-atom $s born)
             $s))))
     (rho:recv (affine:chan 1) $x (rho:drop $x))))

(= (affine:nested-owned-space-once)
   (rho:par
     (rho:send (affine:chan 2)
       (rhometta:eval
         (let $outer (new-space)
           (let $inner (new-space)
             (let $_ (add-atom $inner leaf)
               (let $_ (add-atom $outer (holds $inner))
                 $outer))))))
     (rho:recv (affine:chan 2) $x (rho:drop $x))))

(= (affine:bind-owned-space-once)
   (rho:par
     (rho:send (affine:chan 3)
       (rhometta:eval
         (let $s (new-space)
           (let $_ (add-atom $s via-bind)
             (let $_ (bind! &payload-bound $s)
               (let $_ (add-atom &payload-bound rebound)
                 &payload-bound))))))
     (rho:recv (affine:chan 3) $x (rho:drop $x))))

(= (affine:owned-space-ok)
   (let $owned (unwrap-rho-val (rhometta:run (affine:owned-space-once)))
     (let $_ (add-atom $owned postrun)
       (== (collapse (get-atoms $owned))
           (born postrun)))))

(= (affine:nested-owned-space-ok)
   (let $owner (unwrap-rho-val (rhometta:run (affine:nested-owned-space-once)))
     (let $child (match $owner (holds $x) $x)
       (let $_ (add-atom $child postrun)
         (== (collapse
               (match $owner
                 (holds $x)
                 (collapse (get-atoms $x))))
             ((leaf postrun)))))))

(= (affine:bind-owned-space-ok)
   (let $owned (unwrap-rho-val (rhometta:run (affine:bind-owned-space-once)))
     (let $_ (add-atom $owned postrun)
       (== (collapse (get-atoms $owned))
           (via-bind rebound postrun)))))

!(pragma! num-threads 2)

!(assertEqualToResult (affine:owned-space-ok) (True))
!(assertEqualToResult (affine:nested-owned-space-ok) (True))
!(assertEqualToResult (affine:bind-owned-space-ok) (True))
!(assertEqualToResult (affine:owned-space-ok) (True))
!(assertEqualToResult (affine:nested-owned-space-ok) (True))
!(assertEqualToResult (affine:bind-owned-space-ok) (True))
!(assertEqualToResult (affine:owned-space-ok) (True))
!(assertEqualToResult (affine:nested-owned-space-ok) (True))
!(assertEqualToResult (affine:bind-owned-space-ok) (True))
