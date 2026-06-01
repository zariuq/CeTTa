; Repro: translated singleton-visible-witness packets must collapse repeated
; equal-binding witnesses to one visible witness.

(= (Truth_Deduction $__tr_head_arg_1 $__tr_head_arg_2)
   (unify $__tr_head_arg_1 (stv 0.9 0.9)
          (unify $__tr_head_arg_2 (stv 0.9 0.9)
                 (stv 0.8166666666666668 0.9)
                 Empty)
          Empty))

(= (sentence $__tr_head_arg_3 $__tr_head_arg_4)
   (unify $__tr_head_arg_3 (Inheritance a b)
          (unify $__tr_head_arg_4 (stv 0.9 0.9)
                 (singleton-visible-witness True)
                 Empty)
          Empty))

(= (sentence $__tr_head_arg_5 $__tr_head_arg_6)
   (unify $__tr_head_arg_5 (Inheritance b c)
          (unify $__tr_head_arg_6 (stv 0.9 0.9)
                 (singleton-visible-witness True)
                 Empty)
          Empty))

(= (sentence $__tr_head_arg_7 $TV)
   (unify $__tr_head_arg_7 (Inheritance a c)
          (singleton-visible-witness
            (and (and (sentence (Inheritance a b) $T1)
                      (sentence (Inheritance b c) $T2))
                 (unify $TV (Truth_Deduction $T1 $T2) True Empty)))
          Empty))

!(assertEqual
   (let $vals (collapse (let $derivation (sentence (Inheritance a c) $TV) $TV))
        (size-atom $vals))
   1)

!(assertEqual
   (collapse (let $derivation (sentence (Inheritance a c) $TV) $TV))
   ((stv 0.8166666666666668 0.9)))
