; A Vanilla generalisation problem, written by the forest's SWI front end: the
; background, the metarules, the signature, the metasubstitutions the
; constraints allow, the examples and the bound.
(m head (cell $v0 $v1) $v0)
(m tail (cell $v0 $v1) $v1)
(metarule identity $v0 (m identity $v0 $v1) (m $v0 $v2 $v3) ((m $v1 $v2 $v3)))
(metarule tailrec $v0 (m tailrec $v0 $v1) (m $v0 $v2 $v3) ((m $v1 $v2 $v4) (m $v0 $v4 $v3)))
(= (forest:signature) (f))
(allowed (m identity f head))
(allowed (m identity f tail))
(allowed (m tailrec f head))
(allowed (m tailrec f tail))
(= (forest:allowed $s) (if (== (collapse (match &self (allowed $s) $s)) ()) False True))
(= (task-examples) ((m f (cell 7 (cell 11 (cell 12 (cell 14 (cell 22 (cell 28 (cell 39 (cell 40 (cell 43 (cell 45 (cell 48 (cell 51 (cell 52 (cell 60 (cell 70 (cell 85 (cell 95 (cell 98 (cell 99 nil))))))))))))))))))) 40) (m f (cell 57 (cell 98 nil)) 57) (m f (cell 7 (cell 11 (cell 14 (cell 17 (cell 22 (cell 22 (cell 22 (cell 33 (cell 41 (cell 50 (cell 74 (cell 78 (cell 90 (cell 93 (cell 96 (cell 98 nil)))))))))))))))) 33) (m f (cell 2 (cell 6 (cell 8 (cell 15 (cell 20 (cell 24 (cell 25 (cell 26 (cell 28 (cell 29 (cell 35 (cell 37 (cell 41 (cell 49 (cell 50 (cell 52 (cell 57 (cell 60 (cell 61 (cell 62 (cell 64 (cell 66 (cell 66 (cell 70 (cell 71 (cell 85 (cell 88 (cell 95 (cell 99 (cell 100 nil)))))))))))))))))))))))))))))) 20)))
(= (task-bound) (steps 8))
