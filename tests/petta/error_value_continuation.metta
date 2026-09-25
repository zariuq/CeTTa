; An Error that is a value is an ordinary answer, so a file goes on after
; one: an error caught at top level, a literal Error, one a function builds
; and one a function passes through.  Only an uncaught error stops a file.
(= (mkerr $x) (Error $x oops))
(= (myid $x) $x)
!(catch (translatePredicate (throw foo)))
!(println! after-caught)
!(Error foo bar)
!(println! after-literal)
!(mkerr 5)
!(println! after-built)
!(myid (Error a b))
!(println! after-passed)
!(let (Error $formal $context) (catch (/ 1 0)) (Error $formal caught))
!(println! done)
