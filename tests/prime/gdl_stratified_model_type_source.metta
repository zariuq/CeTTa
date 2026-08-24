(gdl-type-source-v1
  (source-digest "179b5302da67549b12a4247ed4b801477de8fd4e6e8970fffe1a11a1ebdada92")
  (profile-digest "1e2d231641bccb45f17fd833e3fc0a4b49a602e743cdb5339ddbef10d79d75d5")
  (revision gdl-type-source-8a047fd6c05c015ee93fa2a822212f03a206c6633e0daade0188dd29c531548e)
  (source-text "(seed a)\n(seed b)\n(blocked b)\n(<= (reach ?x) (or (seed ?x) (alt ?x)))\n(<= (open ?x) (seed ?x) (not (blocked ?x)))\n(<= (same ?x) (seed ?x) (distinct ?x b))\n(<= (loop ?x) (reach ?x))\n(<= (reach ?x) (loop ?x))\n")
  (profile-text "a, b :: item.\nseed, alt, blocked, reach, open, same, loop :: item -> bool.\n"))
