(: a A)
(: b B)
(: blacksmith (-> Metal Sword))
(: blacksmith (-> Metal Paperclip))
(: iron Metal)

!(test (get-type $a) $z)
!(test (get-type (a b)) (A B))
!(test
  (collapse (get-type (iron blacksmith)))
  ((Metal (-> Metal Sword)) (Metal (-> Metal Paperclip))))
