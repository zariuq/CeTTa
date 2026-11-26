; lib_pln's induction and abduction rules pass the shared (middle) term second,
; as Truth_Induction and Truth_Abduction expect (upstream PLN 9f2b25a).  Every
; premise's strength equals the prior of its target, so each conclusion's
; strength is the prior of C, 0.25.  The earlier argument order gave 0.5 for
; induction and 0.6666666666666666 for abduction.
!(import! &self lib_pln)

(= (STV A) (stv 0.5 0.9))
(= (STV B) (stv 0.25 0.9))
(= (STV C) (stv 0.25 0.9))
(= (STV D) (stv 0.5 0.9))

; Induction: the shared term A is the source of both premises.
!(assertEqual
  (let ($conclusion (stv $strength $confidence))
       (|- ((Inheritance A B) (stv 0.25 0.9)) ((Inheritance A C) (stv 0.25 0.9)))
       ($conclusion $strength))
  ((Inheritance B C) 0.25))
; Abduction: the shared term D is the target of both premises.
!(assertEqual
  (let ($conclusion (stv $strength $confidence))
       (|- ((Inheritance B D) (stv 0.5 0.9)) ((Inheritance C D) (stv 0.5 0.9)))
       ($conclusion $strength))
  ((Inheritance B C) 0.25))
; Deduction keeps its order: the middle term B is second.
!(assertEqual
  (let ($conclusion (stv $strength $confidence))
       (|- ((Inheritance A B) (stv 0.25 0.9)) ((Inheritance B D) (stv 0.5 0.9)))
       ($conclusion $strength))
  ((Inheritance A D) 0.5))
