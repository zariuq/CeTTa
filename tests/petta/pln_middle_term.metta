; PLN's induction and abduction rules pass the shared (middle) term second, as
; Truth_Induction and Truth_Abduction expect (stable-fixed 56c227f, upstream
; PLN 9f2b25a).  Every premise's strength equals the prior of its target, so
; each conclusion's strength is the prior of C, 0.25.  The earlier argument
; order answered 0.5 for induction and 0.6666666666666666 for abduction.
!(import! &self (library lib_pln))

(= (STV A) (stv 0.5 0.9))
(= (STV B) (stv 0.25 0.9))
(= (STV C) (stv 0.25 0.9))
(= (STV D) (stv 0.5 0.9))

; Induction: the shared term A is the source of both premises.
!(test (collapse (|- ((Inheritance A B) (stv 0.25 0.9))
                     ((Inheritance A C) (stv 0.25 0.9))))
       (((Inheritance B C) (stv 0.25 0.4736842105263158))))
; Abduction: the shared term D is the target of both premises.
!(test (collapse (|- ((Inheritance B D) (stv 0.5 0.9))
                     ((Inheritance C D) (stv 0.5 0.9))))
       (((Inheritance B C) (stv 0.25 0.4736842105263158))))
; Deduction keeps its order: the middle term B is second.
!(test (collapse (|- ((Inheritance A B) (stv 0.25 0.9))
                     ((Inheritance B D) (stv 0.5 0.9))))
       (((Inheritance A D) (stv 0.5 0.9))))
