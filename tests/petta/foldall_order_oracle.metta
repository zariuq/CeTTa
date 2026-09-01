; PeTTa does not promise a semantic traversal order for nondeterministic
; results.  These families compare sorted observations while retaining exact
; multiplicity, and separately verify that foldall demands each yielded value.

(= (ordered-source) (a))
(= (ordered-source) (b))
(= (ordered-source) (c))

(= (ordered-alias) (ordered-source))

(= (guarded-source) (if True (ga) (empty)))
(= (guarded-source) (if True (gb) (empty)))
(= (guarded-source) (if False (dead) (gc)))

!(add-atom &fold-order (space-a))
!(add-atom &fold-order (space-b))
!(add-atom &fold-order (space-c))

!(println! (relation-bag
    (msort (foldall union-atom (ordered-source) ()))))

!(println! (superpose-bag
    (msort
        (foldall union-atom
                 (superpose ((sa) (sb) (sc)))
                 ()))))

!(println! (space-bag
    (msort
        (foldall union-atom
                 (match &fold-order ($item) ($item))
                 ()))))

!(println! (alias-bag
    (msort (foldall union-atom (ordered-alias) ()))))

!(println! (demanded-guard-bag
    (msort (foldall union-atom (guarded-source) ()))))

!(println! (duplicate-bag
    (msort
        (foldall union-atom
                 (superpose ((dup) (middle) (dup)))
                 ()))))
