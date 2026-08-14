; A canonical list result survives inferred-signature publication.  The
; producer is intentionally undeclared; strict mode can accept its consumer
; only when inference retains the positive list evidence.

(= (collected-numbers)
   (collapse (superpose (1 2))))

(: consume-numbers (-> (List Number) Bool))
(= (consume-numbers $values) True)

(: use-collected-numbers (-> Bool))
(= (use-collected-numbers)
   (consume-numbers (collected-numbers)))
