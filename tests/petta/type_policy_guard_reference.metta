; Both successful and failing observations are checked against SWI-PeTTa.
(: pcg-meta (-> Grounded Atom))
(= (pcg-meta $x) OK)
!(pcg-meta 6)
!(pcg-meta +)

(: pcg-not-symbol (-> Symbol Atom))
(= (pcg-not-symbol $x) OK)
!(collapse (pcg-not-symbol 6))

; A successful exact query does not get a second chance via metatype when
; a later argument fails. The shared formal variable must stay Number.
(: pcg-later (-> $t $t Atom))
(= (pcg-later $x $y) OK)
!(collapse (pcg-later 6 +))

; A ground value meets a closed formal by the first answer of its guard.
; Authored get-type clauses follow the intrinsic relation, so this one adds
; no second call.
(= (get-type 6) Number)
(: pcg-exact (-> Number Atom))
(= (pcg-exact $x) OK)
!(collapse (pcg-exact 6))

; Authored exact metatype answers also take priority over the metatype
; fallback; they do not turn a data value into executable source.
(= (get-type 7) Grounded)
!(collapse (pcg-meta 7))

(: pcg-unknown (-> %Undefined% Atom))
(= (pcg-unknown $x) OK)
!(pcg-unknown (+ 2 4))
