; Accepted, refused, parked and unexamined authored admissions.
; The residual in each Explained value remains the original resumable handle.
!(import! &self ../../lib/native_history.metta)

(= (debug:allow $coefficient) True)
(= (debug:reject $coefficient) False)
(= (debug:unknown $coefficient) Maybe)
(=w (debug:accepted) (println! "ACCEPTED-BODY") 2 (admit debug:allow))
(=w (debug:rejected) (println! "REJECTED-BODY") 2 (admit debug:reject))
(=w (debug:pending) (println! "PENDING-BODY") 2 (admit debug:unknown))
!(let $observation (select 0 (search-policy (control FIFO (demand all) (retain True) (budget 1000) (history (selections 256) (nodes 32) (bytes 65536) (owners 10000)))) (debug:accepted)) (history:explain $observation))
!(let $observation (select 0 (search-policy (control FIFO (demand all) (retain True) (budget 1000) (history (selections 256) (nodes 32) (bytes 65536) (owners 10000)))) (debug:rejected)) (history:explain $observation))
!(let $observation (select 0 (search-policy (control FIFO (demand all) (retain True) (budget 1000) (history (selections 256) (nodes 32) (bytes 65536) (owners 10000)))) (debug:pending)) (history:explain $observation))
!(let $observation (select 0 (search-policy (control FIFO (demand all) (retain True) (budget 0) (history (selections 256) (nodes 32) (bytes 65536) (owners 10000)))) (debug:pending)) (history:explain $observation))
