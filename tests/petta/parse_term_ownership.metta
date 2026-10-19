; Fresh words occur only inside strings, so source loading cannot intern them.
; The parsed result must outlive the reader's temporary universe, including
; nested expressions, spliced lists and lists with a variable tail.
!(bind! &held (new-state ()))
!(change-state! &held (sread "(fresh-head (fresh-word \"quoted\"))"))
!(get-state &held)
!(change-state! &held (sread "[fresh-word | [fresh-tail]]"))
!(get-state &held)
!(let $p (sread "[fresh-word | $tail]") (let $q (sread "[fresh-word | $tail]") (=alpha $p $q)))
