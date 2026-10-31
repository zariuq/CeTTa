!(import! &self "../../examples/native_space_invariants.metta")
!(import! &self "../../examples/native_kb_server.metta")
(= (danger) (println! ACCIDENTAL_EXECUTION))

; Keep the existing transport/read handlers, replacing the unrestricted edit
; route with this application's versioned constraint policy.
(= (invariant-server:configuration)
  (let (ValueServiceV1 $socket $data $routes $limits) (kb-server:configuration)
    (ValueServiceV1 $socket $data (invariant-server:routes $routes) $limits)))
(= (invariant-server:routes $routes)
  (if (== $routes ()) ()
    (let* (($route (car-atom $routes)) ($rest (invariant-server:routes (cdr-atom $routes))))
      (case $route
        (((ServiceRouteV1 Commit $right $handler)
          (cons-atom (ServiceRouteV1 Apply Write invariant-server:apply) $rest))
         ($other (cons-atom $other $rest)))))))
(= (invariant-server:apply (ServiceCallV1 (KbConfigurationV1 $owner) $peer $view (InvariantV1 $collection $request $command)))
  (invariants:commit $owner $view $collection $request $command))

; The worker supplies (quote request); the entry admits that value wrapper.
; Individual policy/query arguments below retain their explicit Atom types.
(= (invariants-fixture:run Echo) Echo)
(= (invariants-fixture:run (Apply $path $request $command))
  (let* (((StoreOpened $owner) (store:open $path "existing"))
         ((StoreSnapshot $view $epoch $revision) (store:snapshot $owner)))
    (invariants:commit $owner $view sample $request $command)))
(= (invariants-fixture:run (Probe $path $pattern $template $budget))
  (let* (((StoreOpened $owner) (store:open $path "existing"))
         ((StoreSnapshot $view $epoch $revision) (store:snapshot $owner)))
    (durable-space:probe $view sample $pattern $template $budget)))
