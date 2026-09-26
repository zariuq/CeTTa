; A bounded select observes a finite prefix: never more answers than its
; bound, however many threads evaluate the alternatives.
!(select 2 (hyperpose (a b c)))
!(select 2 (hyperpose (a a a)))
!(select 3 (hyperpose (a b c)))
!(select 4 (hyperpose (a b c)))
!(select 0 (hyperpose (a b c)))
!(select 2 (hyperpose ((superpose (a b c)) d)))
