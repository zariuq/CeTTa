; A finite program graph: parse/check cycle, duplicate emit->parse edges,
; an isolated program, and a property referring to one edge occurrence.
(KbFact lex program ("lex.metta"))
(KbFact parse program ("parse.metta"))
(KbFact check program ("check.metta"))
(KbFact emit program ("emit.metta"))
(KbFact isolated program ("isolated.metta"))
(KbFact dep1 dependency (parse lex))
(KbFact dep2 dependency (check parse))
(KbFact dep3 dependency (emit check))
(KbFact dep4 dependency (emit parse))
(KbFact dep5 dependency (parse check))
(KbFact dep6 dependency (emit parse))
(KbFact location1 location (dep4 "emit.metta" 12))
