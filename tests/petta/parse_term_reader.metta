; parse and sread read a term as source forms are read: $_ is a fresh
; variable at each occurrence, any other spelling names one variable,
; True and False are the booleans and 2/3 is a word.
!(let $p (parse "(Pair $_ $_)") (let (Pair $a $b) $p (== $a $b)))
!(let $p (parse "(Triple $_ $x $_)") (let (Triple $a $b $c) $p (== $a $c)))
!(let $p (parse "(Pair $_ $_)") (let (Pair $a $b) $p (let $a 1 (let $b 2 ok))))
!(let $p (sread "(Pair $_ $_)") (let (Pair $a $b) $p (== $a $b)))
!(let $p (parse "(Pair $x $x)") (let (Pair $a $b) $p (== $a $b)))
!(let $p (parse "(Pair $_named $_named)") (let (Pair $a $b) $p (== $a $b)))
!(let (Pair $a $b) (Pair $_ $_) (== $a $b))
!(parse "(f True False)")
!(parse "(f 2/3)")
!(sread "(f 2/3)")
!(parse "(f \"x y\" 1.5)")
