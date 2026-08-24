(rhometta:run-values
  (gslt-rho:process:DirectGroundDifferentV1
    QueryDirectGroundDifferentAcceptedRhoCliV1
    (DirectGroundDifferentV1 DirectLeftV1 DirectRightV1)))
(rhometta:run-values
  (gslt-rho:process:DirectGroundDifferentV1
    QueryDirectGroundDifferentRefusedRhoCliV1
    (DirectGroundDifferentV1 DirectSameV1 DirectSameV1)))

; Ground disequality is structural, not restricted to atomic terms.
(rhometta:run-values
  (gslt-rho:process:DirectGroundDifferentV1
    QueryDirectGroundDifferentStructuredAcceptedRhoCliV1
    (DirectGroundDifferentV1
      (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1)
      (MM0MathScalarsConsV1 (cp 40) MM0MathScalarsNilV1))))

(rhometta:run-values
  (gslt-rho:process:DirectGroundDifferentV1
    QueryDirectGroundDifferentStructuredRefusedRhoCliV1
    (DirectGroundDifferentV1
      (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1)
      (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1))))

(rhometta:run-values
  (gslt-rho:process:DirectGroundDifferentTwiceV1
    QueryDirectGroundDifferentTwiceAcceptedRhoCliV1
    (DirectGroundDifferentTwiceV1
      DirectFirstV1 DirectSecondV1 DirectThirdV1)))

(rhometta:run-values
  (gslt-rho:process:DirectGroundDifferentTwiceV1
    QueryDirectGroundDifferentTwiceRefusedRhoCliV1
    (DirectGroundDifferentTwiceV1
      DirectFirstV1 DirectSecondV1 DirectFirstV1)))
