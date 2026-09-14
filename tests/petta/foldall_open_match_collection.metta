; A fold over an open Space pattern must consume every matching occurrence,
; including candidates that instantiate the same pattern variables differently.

!(add-atom &fold-open (alpha (payload-a (Bin 10))))
!(add-atom &fold-open (beta (payload-b (Bin 20))))
!(add-atom &fold-open (gamma (payload-c (Bin 10))))
!(add-atom &fold-open (delta no-bin))

!(test
    (msort
        (collapse
            (match &fold-open
                   ($key ($value (Bin $bin)))
                   $key)))
    (alpha beta gamma))

!(test
    (msort
        (foldall union-atom
                 (match &fold-open
                        ($key ($value (Bin $bin)))
                        ($key))
                 ()))
    (alpha beta gamma))
