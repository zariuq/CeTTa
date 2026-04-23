; Ported from /home/zar/claude/hyperon/PeTTa/examples/functionhead.metta

(= (myfunc $A $B)
   (append (append (42) $A) $B))

(= (h_old $A $C)
   (if (= $A (myfunc (10) $B))
       ($B $C)
       (empty)))

(= (h (myfunc (10) $B) $C)
   ($B $C))

!(test (h (42 10 40) 42000)
       ((40) 42000))

!(test (h_old (42 10 40) 42000)
       ((40) 42000))
