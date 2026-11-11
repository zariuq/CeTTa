; clingo behind the asp library's names: import-as! reads the clingo library
; with asp: in place of clingo:, so a program written against asp:solve and
; asp:models runs on clingo, its answers in the same shapes with clingo as
; their evidence.  asp:trace, which clingo does not give, stays as written.
!(import-as! &self asp clingo)
!(asp:solve ((rule a () ()) (rule b (a) ())))
!(asp:solve ((choice x () ()) (constraint () (x)) (constraint (x) ())))
!(let (models $ms $evidence) (asp:models ((choice x () ()) (choice y () ()) (constraint (x y) ())))
   ((size-atom $ms) $evidence))
!(let (models $ms $evidence) (asp:models (asp:ground "{p(1..3)}. :- p(1), p(2). :- not p(3).")) ((size-atom $ms) $evidence))
!(asp:solve "a :- not b. b :- not a. :- a.")
!(asp:trace ((rule a () ())))
