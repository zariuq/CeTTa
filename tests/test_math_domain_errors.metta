; CeTTa alpha policy: real-only domain violations return explicit structured
; errors instead of silently poisoning evaluation with NaN/inf.

!(assertEqualToResult
   (sqrt-math -4)
   ((Error (sqrt-math -4) (MathDomainError 1 NonNegativeReal))))
!(assertEqualToResult
   (asin-math 2)
   ((Error (asin-math 2) (MathDomainError 1 ClosedUnitInterval))))
!(assertEqualToResult
   (acos-math -2)
   ((Error (acos-math -2) (MathDomainError 1 ClosedUnitInterval))))
!(assertEqualToResult
   (log-math 1 10)
   ((Error (log-math 1 10) (MathDomainError 1 PositiveRealNotOne))))
!(assertEqualToResult
   (log-math 10 0)
   ((Error (log-math 10 0) (MathDomainError 2 PositiveReal))))
!(assertEqualToResult
   (pow-math -2 0.5)
   ((Error (pow-math -2 0.5) (MathDomainError 2 IntegralExponentWhenBaseNegative))))
!(assertEqualToResult
   (pow-math 0 -1)
   ((Error (pow-math 0 -1) (MathDomainError 1 NonZeroBaseWhenExponentNegative))))
