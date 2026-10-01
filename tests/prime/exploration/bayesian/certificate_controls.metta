; Raw certificate controls and exact rational logarithm enclosures.
; Lean: CertifiedLogarithm.enclosure, CertifiedLogScore.enclosure,
; CertifiedFiniteChoice.checkIntervals_sound, FiniteBayes.update_sound.
!(pragma! prefer-rationals True)
!(import! &self ../../../../lib/probability/finite_bayes.metta)
!(bayes:log-interval 1 3)
!(bayes:log-interval 2 2)
!(bayes:log-interval 0 2)
!(bayes:log-interval 2 -1)
!(bayes:resume-comparison 5
   (bayes:comparison wrong 100 () ((good (4 4)) (wrong (0 0)))))
!(bayes:resume-comparison 5
   (bayes:comparison wrong 0 () ((good (4 4)) (wrong (0 0)))))
!(bayes:certify-order 4 wrong ((good (4 4)) (wrong (0 0))) () ())
!(bayes:coupling? ((a 1/2) (b 1/2)) ((a 1/2) (b 1/2))
   (((a a) 1/2) ((b b) 1/2) ((x x) 1)))
!(bayes:update ((a 1/2) (b 1/2)) ((a -1) (b 2)))
!(bayes:update ((a 1/2) (b 1/2)) ((a 1)))
