; Proc parameters denote computation; Atom parameters preserve syntax.
(: keep (-> Proc Proc))
(= (keep $x) (got $x))
!(keep (+ 1 2))
(: hold (-> Atom Proc))
(= (hold $x) (got $x))
!(hold (+ 1 2))
(: Box (-> Proc Box))
!(Box (+ 1 2))
(: HBox (-> Atom HBox))
!(HBox (+ 1 2))

; The standard library is written in HE, where an `Atom` position holds its
; argument, and its declarations keep that meaning.
!(filter-atom (1 2 3) $x (== $x 3))
!(map-atom (1 2 3) $x (+ $x 1))

; A held part taken out of a record compares as the syntax it holds.
(: Verdict (-> Atom Atom Proc))
(= (established $trace)
   (let ((Verdict $status $program) $rest) (decons-atom $trace)
     (if (== $status Established) yes no)))
!(established ((Verdict Established (program 1))))
!(established ((Verdict Refuted (program 2))))
