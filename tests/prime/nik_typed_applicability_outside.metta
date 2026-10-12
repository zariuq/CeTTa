; Outside the grounded fragments nothing prunes a typed alternative: an
; argument whose type is unknown, or that has conflicting declarations, keeps
; every alternative for the ordinary applicability check.
(: outside-choice (-> String Number))
(: outside-choice (-> Number Number))
(= (outside-choice $value) $value)
(: vague %Undefined%)
(: twofold Number)
(: twofold String)
!(outside-choice vague)
!(outside-choice twofold)
