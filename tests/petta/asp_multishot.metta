; Multi-shot: constraints added to a session between answers, the search
; going on with its learned nogoods.  The models of two choices without both,
; enumerated by excluding each one found; then the last answer, a refutation
; whose input steps are the added constraints (the checker accepts them only
; as such).  A constraint over an atom the program never mentions is never met.
!(import! &self asp)
!(let $s (asp:start ((choice a () ()) (choice b () ()) (constraint (a b) ())))
   (let* (($m1 (asp:next $s))
          ($c1 (asp:constrain $s ((constraint (index-atom $m1 1) ()))))
          ($m2 (asp:next $s))
          ($c2 (asp:constrain $s ((constraint () (a b)))))
          ($m3 (asp:next $s))
          ($c3 (asp:constrain $s ((constraint (a) ()) (constraint (b) ()))))
          ($m4 (asp:next $s)))
     ($m1 $m2 $m3 $m4)))
!(let $s (asp:start ((choice a () ())))
   (let* (($m1 (asp:next $s))
          ($c1 (asp:constrain $s ((constraint (nowhere) ()))))
          ($m2 (asp:next $s))
          ($closed (asp:close $s)))
     ($m1 $c1 $m2 $closed)))
