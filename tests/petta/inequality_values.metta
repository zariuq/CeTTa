; != reduces for every value, empty expressions included, and inside the
; conditions of function bodies.
!(!= () ())
!(!= () (a))
!(!= 1 2)
!(!= a a)
!(!= (f 1) (f 2))
!(!= "x" "x")
(= (more-than-none $xs) (if (!= $xs ()) some none))
!(more-than-none ())
!(more-than-none (1))
