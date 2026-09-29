; The list library on the PeTTa host, imported as a library from its source
; path: every operation over a list value returns a list value, over an
; expression an expression, and a list is data whose elements are never
; evaluated.
!(import! &self ../../lib/list.metta)

!(println! (list:len [a b c]))
!(println! (list:len (a b c)))
!(println! (list:at [a b c] 1))
!(println! (list:cons z [a b]))
!(println! (list:cons z (a b)))
!(println! (list:push [a b] c))
!(println! (list:append [a b] [c]))
!(println! (list:append (a b) (c)))
!(println! (list:reverse [a b c]))
!(println! (list:reverse (a b c)))
!(println! (list:first [a b c]))
!(println! (list:rest [a b c]))
!(println! (IsList (list:is-list [a]) (list:is-list (a))))
!(println! (IsEmpty (list:is-empty []) (list:is-empty [a])))
!(println! (list:like [x] (a b)))
!(println! (list:like (x) (a b)))
!(println! (list:remove-all a [a b a c]))
!(println! (list:unique [a b a c b]))
!(println! (list:unique-by list:atom-equal [a b a]))
!(println! (list:take 2 [a b c]))
!(println! (list:take 5 (a b)))
!(println! (list:from-expression (a b)))
!(println! (list:to-expression [a b]))
!(println! (list:key-by list:len [[a] [b c]]))
; Elements are data: a call written inside a list is not run by the walk.
!(println! (list:len [(+ 1 2) (undefined-function x)]))
!(println! (list:reverse [(+ 1 2) (undefined-function x)]))
