(gdl-type-source-v1
  (source-digest "c53f1cca7aebaa39840a485df9bb4a50cf3caab82b828aa3fde11e6009b77f2f")
  (profile-digest "887a2b93086be14bd81be9d860c6ab4baf0483f9444ff15e1327ce162ffc6d77")
  (revision gdl-type-source-6ec0725b3621e51375e651d0d0ec2da15c2b8cfc772fdb7ca25466a67cfa2ac9)
  (source-text "(index 1)\n(index 2)\n(index 3)\n(<= (distinctcell ?u ?v ?x ?y) (index ?u) (index ?x) (distinct ?u ?x) (index ?v) (index ?y))\n(<= (distinctcell ?u ?v ?x ?y) (index ?v) (index ?y) (distinct ?v ?y) (index ?u) (index ?x))\n")
  (profile-text "1, 2, 3 :: pos.\nindex :: pos -> bool.\ndistinctcell :: pos -> pos -> pos -> pos -> bool.\n"))
