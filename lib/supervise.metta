; Closed data is persisted; quote expressions that must remain inert.
; complete/reconcile retry authorization requires an operation-specific contract.
; acknowledge records local terminal accounting, not remote delivery.

(= (supervise:open $path) (__cetta_lib_supervise_open $path))
(= (supervise:close $ledger) (__cetta_lib_supervise_close $ledger))
(= (supervise:epoch $ledger) (__cetta_lib_supervise_epoch $ledger))
(= (supervise:submit $ledger $id $budget $payload)
   (__cetta_lib_supervise_submit $ledger $id $budget $payload))
(= (supervise:get $ledger $id) (__cetta_lib_supervise_get $ledger $id))
(= (supervise:tasks $ledger) (__cetta_lib_supervise_tasks $ledger))
; Counts: ready, active, uncertain, succeeded, dead-letter, unresolved, acknowledged.
(= (supervise:queue $ledger) (__cetta_lib_supervise_queue $ledger))
(= (supervise:next $ledger) (__cetta_lib_supervise_next $ledger))
(= (supervise:release $permit) (__cetta_lib_supervise_release $permit))
(= (supervise:claim $permit) (__cetta_lib_supervise_claim $permit))
(= (supervise:checkpoint $ledger) (__cetta_lib_supervise_checkpoint $ledger))
(= (supervise:abandon $ledger $id) (__cetta_lib_supervise_abandon $ledger $id))
(= (supervise:uncertain $ledger $epoch $id $generation)
   (__cetta_lib_supervise_uncertain $ledger $epoch $id $generation))
(= (supervise:acknowledge $ledger $epoch $id $generation)
   (__cetta_lib_supervise_acknowledge $ledger $epoch $id $generation))
(= (supervise:reply $ledger $epoch $id $generation $kind $succeeded $retry-safe $receipt)
   (__cetta_lib_supervise_reply $ledger $epoch $id $generation $kind $succeeded $retry-safe $receipt))

; Only this successful claim invokes the handler. An ordinary poll or repeated
; evaluation does not authorize a second invocation. The handler's local catch
; and outcome policy remain explicit; there is no implicit retry or completion.
(= (supervise:dispatch-once $permit $handler)
   (case (supervise:claim $permit)
     (((supervise:dispatch $epoch $id $generation $payload)
        ($handler $payload $epoch $id $generation))
      ($other $other))))
