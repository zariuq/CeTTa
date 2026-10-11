!(import! &self ../../lib/hol_light/data.metta)
!(import! &self ../../lib/hol_light/syntax.metta)
!(import! &self ../../lib/hol_light/kernel.metta)
!(import! &self ../../lib/hol_light/definitions.metta)
!(import! &self ../../lib/hol_light/derived.metta)
!(import! &self ../../lib/hol_light/profile.metta)
!(import! &self ../../lib/hol_light/receive.metta)

(= (type-publication:identity)
  (Abs (Var "x" (hol-light:bool)) (Var "x" (hol-light:bool))))
(= (type-publication:before)
  (case (hol-light:step (hol-light:initial HOL:Nil) "existence"
          (REFL (type-publication:identity)))
    (((Some $world) $world) ($other malformed-existence))))
(= (type-publication:request)
  (TYPE_DEFINITION "identity-carrier" "abs-identity" "rep-identity" "existence"))
(= (type-publication:define $world)
  (hol-light:step $world "definition" (type-publication:request)))
(= (type-publication:after)
  (case (type-publication:define (type-publication:before))
    (((Some $world) $world) ($other malformed-definition))))
(= (type-publication:has-state $result)
  (case $result (((Some (HOL:State $op $c $a $cache $receipts)) True) ($other False))))
(= (type-publication:origin $key)
  (case (hol-light:known (type-publication:after) $key)
    (((Some (HOL:Thm HOL:Nil $conclusion HOL:Nil $origin))
      (== $origin (HOL:Origin $key (type-publication:request))))
     ($other False))))
(= (type-publication:receipts)
  (case (type-publication:after)
    (((HOL:State $op $c $a $cache
       (HOL:Cons (HOL:Receipt (HOL:RepKey "definition") $second (HOL:Cons "existence" HOL:Nil))
        (HOL:Cons (HOL:Receipt "definition" $first (HOL:Cons "existence" HOL:Nil)) $rest)))
      (and (== $first (type-publication:request))
           (and (== $second (type-publication:request)) (== (hol-light:length $rest) 1))))
     ($other False))))
(= (type-publication:occupied-receipt $key)
  (case (hol-light:step (type-publication:before) $key (DECLARE_TYPE "unrelated" 0))
    (((Some $world) $world) ($other malformed-receipt))))

; Cache-address occupancy must refuse even a payload without theorem shape.
(= (type-publication:occupied-cache $key)
  (case (type-publication:before)
    (((HOL:State $op $c $a $cache $receipts)
      (HOL:State $op $c $a (HOL:Cons (HOL:Entry $key opaque-payload) $cache) $receipts)))))

!(TypePublicationControl accepted
  (== (type-publication:has-state (type-publication:define (type-publication:before))) True))
!(TypePublicationControl first-origin (== (type-publication:origin "definition") True))
!(TypePublicationControl second-origin (== (type-publication:origin (HOL:RepKey "definition")) True))
!(TypePublicationControl ordered-receipts (== (type-publication:receipts) True))
!(TypePublicationControl first-premise
  (== (hol-light:premise-count (Some (type-publication:after)) "definition") 1))
!(TypePublicationControl second-premise
  (== (hol-light:premise-count (Some (type-publication:after)) (HOL:RepKey "definition")) 1))
!(TypePublicationControl dependencies
  (and (== (hol-light:dependencies (Some (type-publication:after)) "definition") HOL:Nil)
       (== (hol-light:dependencies (Some (type-publication:after)) (HOL:RepKey "definition")) HOL:Nil)))
!(TypePublicationControl old-evidence
  (== (hol-light:known (type-publication:before) "existence")
      (hol-light:known (type-publication:after) "existence")))
!(TypePublicationControl first-receipt-refused
  (== (type-publication:define (type-publication:occupied-receipt "definition")) None))
!(TypePublicationControl first-cache-refused
  (== (type-publication:define (type-publication:occupied-cache "definition")) None))
!(TypePublicationControl second-receipt-refused
  (== (type-publication:define (type-publication:occupied-receipt (HOL:RepKey "definition"))) None))
!(TypePublicationControl second-cache-refused
  (== (type-publication:define (type-publication:occupied-cache (HOL:RepKey "definition"))) None))
!(TypePublicationControl reserved-function-refused
  (== (hol-light:step (type-publication:before) "reserved-definition"
        (TYPE_DEFINITION "fun" "abs-reserved" "rep-reserved" "existence")) None))
!(TypePublicationControl arrow-spelling-distinct
  (== (type-publication:has-state
        (hol-light:step (type-publication:before) "arrow-definition"
          (TYPE_DEFINITION "->" "abs-arrow" "rep-arrow" "existence"))) True))
