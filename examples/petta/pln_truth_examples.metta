; Small executable examples using the existing PLN truth-formula library.
!(import! &self (library lib_pln))

; Implication and its premise combine strength with confidence.
!(println! (ModusPonens
   (Truth_ModusPonens (stv 0.8 0.9) (stv 0.7 0.8))))
!(println! (ZeroStrengthPremise
   (Truth_ModusPonens (stv 0.0 0.9) (stv 0.7 0.8))))

; Two evidence summaries can disagree in strength and still be revised.
!(println! (Revision
   (Truth_Revision (stv 0.8 0.7) (stv 0.3 0.6))))
!(println! (Negation (Truth_Negation (stv 0.8 0.9))))

; Deduction uses the three marginal values and two conditional values.
!(println! (Deduction
   (Truth_Deduction (stv 0.5 0.9) (stv 0.5 0.8) (stv 0.5 0.7)
                    (stv 0.6 0.9) (stv 0.7 0.8))))
!(println! (DeductionZeroPrior
   (Truth_Deduction (stv 0.0 0.9) (stv 0.5 0.8) (stv 0.5 0.7)
                    (stv 0.6 0.9) (stv 0.7 0.8))))

; The IC example factors a repeated fragment inside this computation.
!(println! (TransitiveSimilarity
   (collapse (TransitiveSimilarityStrength 0.5 0.6 0.7 0.8 0.9))))
!(println! (TransitiveSimilarityEmpty
   (collapse (TransitiveSimilarityStrength 0.4 1.0 0.4 0.5 0.5))))
