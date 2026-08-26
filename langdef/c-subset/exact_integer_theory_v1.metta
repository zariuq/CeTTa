(ExactIntegerTheoryV1
  (LCons
    (ExactIntegerOperation "exact-int:add"
      (LCons NumInteger (LCons NumInteger LNil))
      NumInteger BoolFalse CoreAdd)
    (LCons
      (ExactIntegerOperation "exact-int:sub"
        (LCons NumInteger (LCons NumInteger LNil))
        NumInteger BoolFalse CoreSub)
      (LCons
        (ExactIntegerOperation "exact-int:mul"
          (LCons NumInteger (LCons NumInteger LNil))
          NumInteger BoolFalse CoreMul)
        (LCons
          (ExactIntegerOperation "exact-int:tquot"
            (LCons NumInteger (LCons NumInteger LNil))
            NumInteger BoolTrue CoreTQuot)
          (LCons
            (ExactIntegerOperation "exact-int:fquot"
              (LCons NumInteger (LCons NumInteger LNil))
              NumInteger BoolTrue CoreFQuot)
            (LCons
              (ExactIntegerOperation "exact-int:trem"
                (LCons NumInteger (LCons NumInteger LNil))
                NumInteger BoolTrue CoreTRem)
              (LCons
                (ExactIntegerOperation "exact-int:frem"
                  (LCons NumInteger (LCons NumInteger LNil))
                  NumInteger BoolTrue CoreFRem)
                LNil))))))))
