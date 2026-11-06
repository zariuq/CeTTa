; A later rule answers when an earlier rule's premise fails (pick-other's
; guard on A, its Unwrap on B); a repeated variable in a head compares whole
; terms (same-yes); a call's output is matched against its pattern (wrap).
(gslt-presentation-v1 NativeDirectGuardsV1
  (signature
    (operator Pick 2)
    (operator Same 3)
    (operator Wrap 2)
    (operator Unwrap 2)
    (operator A 0)
    (operator B 0)
    (operator Yes 0)
    (operator No 0)
    (operator P 1)
    (operator Pair 2))
  (equations)
  (rewrites
    (rule pick-other
      (head (Pick ?x Yes))
      (body (different ?x A) (Unwrap ?x ?y)))
    (rule pick-a
      (head (Pick A No))
      (body))
    (rule same-yes
      (head (Same ?x ?x Yes))
      (body))
    (rule same-no
      (head (Same ?x ?y No))
      (body (different ?x ?y)))
    (rule unwrap
      (head (Unwrap (P ?x) ?x))
      (body))
    (rule wrap
      (head (Wrap ?x (Pair ?x ?y)))
      (body (Unwrap ?x ?y)))))
