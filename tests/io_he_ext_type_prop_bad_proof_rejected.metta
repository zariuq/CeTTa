; Minimal semantic witness extracted from tests/he_d4_type_prop.metta.
(: Entity Type)
(: Socrates Entity)
(: Human (-> Entity Type))
(: Mortal (-> Entity Type))
(: HumansAreMortal (-> (Human $t) (Mortal $t)))
!(get-type (HumansAreMortal (Human Socrates)))
