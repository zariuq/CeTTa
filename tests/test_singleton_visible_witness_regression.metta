; Regression: translated singleton-visible-witness should collapse repeated
; equal-binding witnesses to one visible witness packet.

(= (Truth_Deduction (stv 0.9 0.9) (stv 0.9 0.9))
   (stv 0.8166666666666668 0.9))

(= (sentence (Inheritance a b) (stv 0.9 0.9))
   (singleton-visible-witness True))
(= (sentence (Inheritance b c) (stv 0.9 0.9))
   (singleton-visible-witness True))
(= (sentence (Inheritance a c) $TV)
   (singleton-visible-witness
     (and (and (sentence (Inheritance a b) $T1)
               (sentence (Inheritance b c) $T2))
          (unify $TV (Truth_Deduction $T1 $T2) True Empty))))

!(assertEqual
   (let $vals (collapse (let $derivation (sentence (Inheritance a c) $TV) $TV))
        (size-atom $vals))
   1)
!(assertEqual
   (collapse (let $derivation (sentence (Inheritance a c) $TV) $TV))
   ((stv 0.8166666666666668 0.9)))
