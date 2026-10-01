; Answers are written when the run completes, and a term that is not a finite
; tree has no text: the queries after such an answer still run and write
; their effects, then the run ends with status 2 and no answers.
; SWI-PeTTa's output and status.
!(println! before)
!(let $_ (translatePredicate (= $c (f $c))) $c)
!(println! after)
!(+ 1 2)
