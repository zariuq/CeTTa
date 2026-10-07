; A crossing set on a scope-forming construct names variables; a symbol
; there is refused, never ignored.
!(let $f (lam z (Pair z $t)){t} ($f 1))
