; Recording publishes to the live space. Qualified replay uses a private
; space image, preserving the full read/take/absence word and its costs.
!(import! &self ../../../lib/native_history.metta)
!(bind! &jobs (new-space))
!(add-atom &jobs (token A))
!(add-atom &jobs (read-only R))
!(add-atom &jobs (exec 0 (I (take Wake)) (O (+ Woke))))
!(add-atom &jobs
   (exec 0 (I (take (token A)) (all (read-only R) $rows) (absent Blocked))
           (O (+ Wake))))

!(bind! &capsule
   (history:capture
      (quote (cost:run
         (search-policy (control FIFO (resources Transaction) (budget 0)
            (demand all) (retain True) (costs native-events-v1) (receipts True)
            (history (selections 512) (nodes 64) (bytes 32768) (owners 512))))
         &jobs))
      (ReplayLimits 8 8192 536870912)))
!(bind! &recorded (history:record &capsule 2000))
!(let $report &recorded (history:field $report status))
!(collapse (get-atoms &jobs))
!(bind! &replayed (history:replay &capsule 2000000000))
!(let $report &replayed (history:field $report replay-authenticated))
!(collapse (get-atoms &jobs))
!(history:dispose &capsule)
