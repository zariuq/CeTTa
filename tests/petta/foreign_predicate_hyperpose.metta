; Workers may repeatedly borrow the adapter's pooled Prolog engine.  Each
; boundary call releases it before another worker can claim it, and the result
; bag must retain every answer from both branches.
!(import_prolog_function between)
!(msort
    (collapse
        (hyperpose
            ((between 1 3)
             (between 4 5)))))
