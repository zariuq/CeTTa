!(import! &self ../lib/lib_wmpln)

; Runtime canary for the Lean BinaryCounts strength taxonomy.
;
; The same evidence counts support several honest strength projections:
; Haldane/MLE uses n+/(n+ + n-), Jeffreys uses a symmetric 1/2 prior,
; and Laplace uses a symmetric 1 prior.  This file makes that degree of
; freedom executable without claiming a full posterior module in CeTTa.

(= (strength-haldane $pos $neg)
   (let $total (+ $pos $neg)
        (if (== $total 0.0)
            0.0
            (/ $pos $total))))

(= (strength-symmetric-prior $pos $neg $prior)
   (/ (+ $pos $prior)
      (+ (+ $pos $neg) (* 2.0 $prior))))

(= (strength-jeffreys $pos $neg)
   (strength-symmetric-prior $pos $neg 0.5))

(= (strength-laplace $pos $neg)
   (strength-symmetric-prior $pos $neg 1.0))

(= (abs-diff $a $b)
   (if (< $a $b)
       (- $b $a)
       (- $a $b)))

; Same counts, different prior choice: the strength is not forced by counts
; until the projection/ prior is named.
!(assertEqual (strength-haldane 0.0 1.0) 0.0)
!(assertEqual (strength-jeffreys 0.0 1.0) 0.25)
!(assertEqual (strength-laplace 0.0 1.0) 0.3333333333333333)
!(assertEqual
  (== (strength-haldane 0.0 1.0)
      (strength-jeffreys 0.0 1.0))
  False)
!(assertEqual
  (== (strength-jeffreys 0.0 1.0)
      (strength-laplace 0.0 1.0))
  False)

!(assertEqual (strength-haldane 1.0 0.0) 1.0)
!(assertEqual (strength-jeffreys 1.0 0.0) 0.75)
!(assertEqual (strength-laplace 1.0 0.0) 0.6666666666666666)

; The generic symmetric-prior projection specializes to the named views.
!(assertEqual
  (strength-symmetric-prior 0.0 1.0 0.0)
  (strength-haldane 0.0 1.0))
!(assertEqual
  (strength-symmetric-prior 0.0 1.0 0.5)
  (strength-jeffreys 0.0 1.0))
!(assertEqual
  (strength-symmetric-prior 0.0 1.0 1.0)
  (strength-laplace 0.0 1.0))

; Proper symmetric priors converge toward the Haldane/MLE view as the same
; empirical ratio is observed with larger total evidence.
!(assertEqual
  (< (abs-diff (strength-haldane 90.0 10.0)
               (strength-laplace 90.0 10.0))
     (abs-diff (strength-haldane 9.0 1.0)
               (strength-laplace 9.0 1.0)))
  True)
!(assertEqual
  (< (abs-diff (strength-haldane 90.0 10.0)
               (strength-jeffreys 90.0 10.0))
     (abs-diff (strength-haldane 9.0 1.0)
               (strength-jeffreys 9.0 1.0)))
  True)
