; A scalar identity comparison, without graph import or policy descriptors.
!(import! &self "../../vendor/tcalc-native/native/p1/p1.metta")
(= (nw:program)
   (p1:program native_word (Regions) (Registers) (Init)
     (Seq (TFund 1) (Arg0 v1) (Arg1 v2) (Cmp Eq v3 v1 v2)
       (If v3 (Seq (Imm v1 1) (Return FIRED v1))
               (Seq (Imm v1 0) (Return FIRED v1))))))
!(let* (($text (system:arg 0)) ($notes (system:arg 1))) (p1:write nw:program $text $notes))
