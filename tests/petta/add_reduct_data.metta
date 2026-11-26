; add-reduct is HE's.  PeTTa defines it only in lib_he, by an equation, so
; until that library is imported an add-reduct call is data, its arguments
; evaluated as any data's are, and nothing is added.
!(add-reduct &self (+ 1 2))
!(add-reduct &reducts (= (reduct-f $x) (+ 1 2)))
!(collapse (add-reduct &reducts (superpose (1 2))))
!(collapse (get-atoms &reducts))
!(let $call (add-reduct &reducts item) $call)
!(get-metatype add-reduct)

; Imported, lib_he's equation runs: it adds the equation whose body is its
; body's tail, evaluated, through add-atom.
!(import! &self (library lib_he))
!(add-reduct &reducts (= (reduct-g) (+ 20 22)))
!(collapse (match &reducts (= (reduct-g) $body) $body))
!(add-reduct &self (= (reduct-h) (+ 1 2)))
!(collapse (reduct-h))
!(catch (add-reduct 42 (= (reduct-k) 1)))
