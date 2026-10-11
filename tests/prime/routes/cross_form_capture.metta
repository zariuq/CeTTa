; Cross-form capture.  An equation is one form and the query another: a
; closure one builds, applied to a name of the other, must not connect them
; by spelling.  (mk) spells its local $w, as the query spells its own name;
; (mk2) spells it $y.  Both answer (Pair 1 5).  The same closures through
; text, lift-built code and a space are in scope_metadata_survival.metta.
(= (mk) (lam x (lam z (let $w z (Pair $w x)))))
(= (mk2) (lam x (lam z (let $y z (Pair $y x)))))
!(row mk)
!(let $w 5 (((mk) $w) 1))
!(row mk2)
!(let $w 5 (((mk2) $w) 1))
