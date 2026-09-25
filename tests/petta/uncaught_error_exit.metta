; An uncaught error ends the file, and the process exits with status 2, as
; SWI-PeTTa's does.  An error that catch returns is a value, and the file
; goes on after it.
(= (bad) (+ 1 a))
!(println! before)
!(catch (bad))
!(println! after-caught)
!(superpose (1 (bad) 3))
!(println! not-reached)
