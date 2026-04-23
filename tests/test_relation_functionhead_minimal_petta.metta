; Minimal native frontier witness distilled from hyperon/PeTTa/examples/functionhead.metta

(= (myfunc $A $B)
   (append (append (42) $A) $B))

(= (h (myfunc (10) $B) $C)
   ($B $C))

!(test (h (42 10 40) 42000)
       ((40) 42000))
