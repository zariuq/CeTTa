!(import! &self ../../../hyperon/PeTTa/lib/lib_he)

!(assertEqual (assertEqual (+ 1 2) (- 6 3)) True)
!(assertEqual (assertAlphaEqual (h $x $y) (h $a $b)) True)
