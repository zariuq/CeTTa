; A raised Prolog error is the Error value PeTTa's catch returns, formal
; term then context, and evaluation goes on.
!(println! (let (Error $formal $context)
                (catch (translatePredicate (atom_length foo bar)))
             $formal))
!(println! (catch (progn (translatePredicate (atom_to_term 42 $t $b)) $t)))
!(println! after)

; A raised error abandons the rest of the protected computation.
!(println! (let (Error $formal $context)
                (catch (progn (translatePredicate (atom_length foo bar))
                              reached-after-the-error))
             $formal))
!(println! (let (Error $formal $context)
                (catch (let $x (translatePredicate (atom_length foo bar))
                            reached-after-the-error))
             $formal))
