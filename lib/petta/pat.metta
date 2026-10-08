; Directional matching is opt-in in extended profiles. A mismatch has no
; answer; a native fault is not a mismatch. Policies select binding permission:
; match% binds left patterns, %match binds right patterns, unify permits both,
; variant returns one simultaneous bijective renaming for the whole batch.
(: pat:solve (-> Atom Atom Atom))
(: pat:apply (-> %Undefined% Atom Atom))
(: pat:view (-> %Undefined% Atom))
; Each query returns (pat:hit rows witness), with one
; (pat:row occurrence original instantiated) per premise, in source order.
(: pat:query (-> %Undefined% Atom Atom Atom))
(: pat:match% (-> %Undefined% Atom Atom %Undefined%))
(: pat:%match (-> %Undefined% Atom Atom Atom %Undefined%))
(: pat:is-instance (-> Atom Atom Bool))
(: pat:is-variant (-> Atom Atom Bool))
(: pat:unify% (-> Atom Atom Atom Atom %Undefined%))
(: pat:let% (-> Atom %Undefined% Atom %Undefined%))
(: pat:case% (-> %Undefined% Atom %Undefined%))
