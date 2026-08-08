; The failing bar branch is delimited by catch.  Backtracking to the sibling
; must remove its in-flight guard and create a fresh guard for foo.

(= (get-type foo) RuntimeOnly)

(= (mark-caught $value)
   (if (= $value (cons Error $_)) caught $value))

!(test
   (collapse
     (let $typed (the RuntimeOnly $value)
          (let (superpose (bar foo)) $candidate
               (mark-caught
                 (catch
                   (let $candidate $value $typed))))))
   (caught foo))
