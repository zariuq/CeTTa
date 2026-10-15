; Replay a bounded pure request, then continue its independently owned residual.
; Recording/replay require a build with causal receipts enabled.
!(import! &self ../../../lib/native_history.metta)

(= (replay:coin) A)
(= (replay:coin) B)
(= (replay:coin) B)
(= (replay:pair $x) (Pair $x $x))

!(bind! &capsule
   (history:capture
      (quote (cost:run
         (search-policy (control FIFO (budget 0) (demand all) (retain True)
            (costs native-events-v1) (receipts True)
            (history (selections 512) (nodes 64) (bytes 32768) (owners 512))))
         (replay:pair (replay:coin))))
      (ReplayLimits 8 8192 134217728)))
!(bind! &recorded (history:record &capsule 7))
!(let $report &recorded (history:field $report status))
!(bind! &replayed (history:replay &capsule 2000000000))
!(let $report &replayed (history:field $report replay-authenticated))

; Structural projection retains the actual owning token. It does not parse a
; printed handle or execute arbitrary code held in the report.
!(bind! &accepted
   (let $report &replayed
      (let (HistoryField Found accepted-control $token)
           (history:field $report accepted-control)
         (let $tail (cdr-atom $token) (car-atom $tail)))))
!(history:dispose &capsule)
; Forgetting keeps this independently owned residual and its paid work.
; A zero inspection limit refuses without consuming it.
; The limit caps source-space inventory and global Atom-domain inspection.
; It does not budget the remaining copy, bytes or elapsed time.
!(bind! &refused (history:forget &accepted 0))
!(let $change &refused (history:field $change status))
!(bind! &forgotten (history:forget &accepted 131072))
!(let $change &forgotten (history:field $change status))
!(let $observation
      (select 0
         (search-policy (control FIFO (budget 1000) (demand all) (retain True)))
         &accepted)
   (history:field $observation values))
