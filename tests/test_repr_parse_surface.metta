; Text surface choices:
; - repr receives data through Atom, so it renders expressions before evaluating
;   them unless the caller explicitly evaluates first.
; - parse is strict and accepts exactly one atom, with delimiter text around it.
; - parse-first keeps stream-reader behavior: it takes the first atom and
;   ignores later text, including malformed trailing text.
!(assertEqual (repr 42) "42")
!(assertEqual (repr 3.5) "3.5")
!(assertEqual (repr True) "True")
!(assertEqual (repr "42") "\"42\"")
!(assertEqual (repr "line\nbreak") "\"line\\nbreak\"")
!(assertEqual (repr (A (B C))) "(A (B C))")
!(assertEqual (repr ()) "()")
!(assertEqual (repr (+ 1 2)) "(+ 1 2)")
!(assertEqual (let $x (+ 1 2) (repr $x)) "3")

!(assertEqual (parse "A") A)
!(assertEqual (parse "(R A B)") (R A B))
!(assertEqual (parse "(R A (S B C))") (R A (S B C)))
!(assertEqual (parse "\"42\"") "42")
!(assertEqual (parse "  (A B) ; trailing comment") (A B))
!(parse "(* 2 21)")
!(assertEqual (let $x (parse "(* 2 21)") (repr $x)) "(* 2 21)")
!(assertEqual (let $x (parse "(A B)") (repr $x)) "(A B)")
!(assertEqualToResult (parse "A B")
  ((Error (parse "A B") ParseFailed)))
!(assertEqualToResult (parse "(A B) C")
  ((Error (parse "(A B) C") ParseFailed)))
!(assertEqualToResult (parse "(A B")
  ((Error (parse "(A B") ParseFailed)))
!(assertEqualToResult (parse "\"")
  ((Error (parse "\"") ParseFailed)))
!(assertEqualToResult (parse "")
  ((Error (parse "") ParseFailed)))

!(assertEqual (parse-first "A B") A)
!(assertEqual (parse-first "(A B) C") (A B))
!(assertEqual (parse-first "A )") A)
!(assertEqualToResult (parse-first "(A B")
  ((Error (parse-first "(A B") ParseFailed)))
!(assertEqualToResult (parse-first "")
  ((Error (parse-first "") ParseFailed)))
