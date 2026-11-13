!(import! &self (library lib_transform))
!(bind! &code-source (new-space))
!(bind! &code-log (new-space))
(= (inc $n) (+ $n 1))
!(add-atom &code-source (row (inc 1)))
; Raw argument positions must receive the value without quote syntax.
!(let-code (collapse (transform &code-source
   (, (row $x) (let $_written (add-atom &code-log (seen $x)))) done)))
!(caller-code (collapse (transform &code-source (row $x)
   (add-atom &code-log (call $x)))))
!(multiple (collapse (transform &code-source (row $x)
   (, (add-atom &code-log (left $x)) (add-atom &code-log (right $x))))))
!(raw-meta (collapse (transform &code-source (row $x) (get-metatype $x))))
!(raw-equal (collapse (transform &code-source (row $x) (== $x 2))))
!(quoted-value (collapse (transform &code-source (row $x) (quote $x))))
!(bare-source (collapse (transform &code-source
   (, (row $x) (let $v $x)) $v)))
; Written computation still runs, separately from the held source value.
!(explicit-code (collapse (transform &code-source
   (, (row $x) (let $v (inc 1))) $v)))
!(stored-code (sort-atom (collapse (get-atoms &code-log))))
; Control positions and destructuring read the held expression as data.
!(once-code (collapse (transform &code-source (row $x)
   (once (superpose ((tag $x) b c))))))
!(destructure (collapse (transform &code-source (row $x)
   (let ($a $b) $x (swap $b $a)))))
!(bind! &code-key (new-space))
!(add-atom &code-key (val (inc 1) hit))
!(add-atom &code-key (val 2 wrong))
!(nested-match (collapse (transform &code-source (row $x)
   (match &code-key (val $x $v) $v))))
!(bind! &code-number (new-space))
!(add-atom &code-number (number 5))
!(captured-lambda (collapse (transform &code-number (number $x)
   ((|-> ($n) (+ $n $x)) 10))))
!(lambda-let (collapse (transform &code-number
   (, (number $x) (let $v ((|-> ($n) (+ $n $x)) 10))) (found $v))))
; The error handler remains the one written by the caller.
!(caught-code (collapse (transform &code-source (row $x)
   (catch (+ $x 1 2)))))
!(scoped-cut (collapse (transform &code-source (row $x)
   (catch (progn (superpose (a b)) (cut) (tag $x))))))
