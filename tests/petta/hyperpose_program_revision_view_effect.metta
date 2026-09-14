; An ignored binding does not erase its effectful computation.  The test
; observes only publication into a shared space, not which evaluator path
; realizes the equation.

(= (revision-view-effect-leaf $value)
   (add-atom &revision-view-effect-space $value))

(= (revision-view-effect-middle $value)
   (let* (
          ($_effect (revision-view-effect-leaf $value)))
      ()))

(= (revision-view-effect-branch)
   (with_mutex revision-view-effect-mutex
      (let $_ignored
           (revision-view-effect-middle (effect-published))
         ())))

; The worker result bag is deliberately ignored; only publication is observed.
(= (revision-view-effect-run)
   (let $_seed
        (add-atom &revision-view-effect-space
                  (revision-view-effect-seed))
        (let $_workers
             (collapse
               (hyperpose
                 ((revision-view-effect-branch)
                  revision-view-effect-companion-a
                  revision-view-effect-companion-b
                  revision-view-effect-companion-c)))
             (collapse
               (match &revision-view-effect-space
                      (effect-published)
                      (effect-published))))))

!(revision-view-effect-run)
