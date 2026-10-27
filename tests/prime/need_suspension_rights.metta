; Prime suspension rights and explicit non-forcing origin observation.

!(bind! &rights-counter (new-state 0))
(= (rights-bump)
   (let $old (get-state &rights-counter)
     (let $_ (change-state! &rights-counter (+ $old 1))
       $old)))

; Inspection returns closed source data and does not demand the origin.
!(let $p (delay (rights-bump))
   (observe-origin $p))
!(get-state &rights-counter)

; Rights only attenuate.  Denied operations are checked faults.
!(assertEqualToResult
   (let $p (delay 7)
     (force (suspension:restrict $p (suspension:rights inspect)))){$p}
   ((Error 7 suspension:force-denied)))
!(assertEqualToResult
   (let $p (delay 7)
     (observe-origin
       (suspension:restrict $p (suspension:rights force)))){$p}
   ((Error (observe-origin suspension:opaque)
           suspension:inspect-denied)))
!(assertEqualToResult
   (let $p (delay 7)
     (suspension:restrict
       (suspension:restrict $p (suspension:rights inspect))
       (suspension:rights force inspect))){$p}
   ((Error
      (suspension:restrict
        suspension:opaque
        (suspension:rights force inspect))
      suspension:rights-escalation)))

; Identity ignores rights, but both public observers require inspect.
!(let $p (delay 7)
   (same-suspension
     $p
     (suspension:restrict $p (suspension:rights inspect))))
!(let $p (delay 7)
   (same-origin
     $p
     (suspension:restrict $p (suspension:rights inspect))))
!(let $left (delay 7)
   (let $right (delay 7)
     (Pair
       (same-suspension $left $right)
       (same-origin $left $right))))
!(let $left (delay 1)
   (let $right (delay (+ 0 1))
     (same-origin $left $right)))
!(assertEqualToResult
   (let $p (delay 7)
     (same-suspension
       (suspension:restrict $p (suspension:rights force))
       $p)){$p}
   ((Error
      (same-suspension suspension:opaque suspension:opaque)
      suspension:inspect-denied)))

; A view is data.  Delaying it allocates a fresh suspension.
!(let $p (delay 9)
   (case (observe-origin $p)
     (((suspension:view $origin)
       (same-suspension $p (delay $origin)))
      ($_ malformed-view))))

; Persistence keeps attenuated rights; it never restores authority.  A
; space stores its argument as written, so a suspension is attenuated before
; it is stored.
!(bind! &rights-inspect-space (new-space))
!(let $p (delay 11)
   (chain (suspension:restrict $p (suspension:rights inspect)) $restricted
     (add-atom &rights-inspect-space (stored $restricted))))
!(assertEqualToResult
   (match &rights-inspect-space (stored $saved)
     (force $saved)){$saved}
   ((Error 11 suspension:force-denied)))
!(match &rights-inspect-space (stored $saved)
   (observe-origin $saved))

!(bind! &rights-force-space (new-space))
!(let $p (delay 13)
   (chain (suspension:restrict $p (suspension:rights force)) $restricted
     (add-atom &rights-force-space (stored $restricted))))
!(match &rights-force-space (stored $saved)
   (force $saved))
!(assertEqualToResult
   (match &rights-force-space (stored $saved)
     (observe-origin $saved)){$saved}
   ((Error (observe-origin suspension:opaque)
           suspension:inspect-denied)))

; Inspecting a fault does not force the references in its diagnostic cause.
; The original promise remains available for a later authorized force.
!(if-error
   (force (suspension:restrict (delay 7) (suspension:rights inspect)))
   caught leaked)
!(if-error (force (delay 7)) caught ordinary)
!(chain
   (force (suspension:restrict (delay 7) (suspension:rights inspect)))
   $fault (if-error $fault caught leaked))
!(let $p (delay (rights-bump))
   (Pair
     (if-error
       (force (suspension:restrict $p (suspension:rights inspect)))
       caught leaked)
     (Pair (get-state &rights-counter)
       (Pair (force $p) (get-state &rights-counter)))))
!(get-state &rights-counter)

; Stored source remains source data until explicitly activated. Merely
; writing a restriction expression does not manufacture a granted handle.
!(bind! &rights-source-space (new-space))
!(add-atom &rights-source-space
   (stored (suspension:restrict (delay (rights-bump)) (suspension:rights inspect))))
!(match &rights-source-space (stored $source) (force $source))
!(get-state &rights-counter)

; An attenuation written inside stored code is code: it never runs, and the
; stored code holds the suspension itself, with all its rights.
!(bind! &rights-code-space (new-space))
!(let $p (delay 15)
   (add-atom &rights-code-space
     (stored (suspension:restrict $p (suspension:rights force)))))
!(match &rights-code-space (stored (suspension:restrict $inner $rights))
   (observe-origin $inner))
