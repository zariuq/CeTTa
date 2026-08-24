; Popper's complete synthesis-finddupl structural background and authored
; two-clause program.  Membership is proof-relevant: repeated equal elements
; retain distinct recursive occurrence paths.

(= (popper:finddupl:package)
  (compile:rule-package popper-synthesis-finddupl-v1
    (rm-package
      (rm-block head popper:synthesis-finddupl:bk-head
        (quote
          (popper:list:proof:head popper:numeral $head $tail))
        rm-nil
        (quote
          (popper:list:head popper:numeral
            (list:cons popper:numeral $head $tail) $head)))
      (rm-block tail popper:synthesis-finddupl:bk-tail
        (quote
          (popper:list:proof:tail popper:numeral $head $tail))
        rm-nil
        (quote
          (popper:list:tail popper:numeral
            (list:cons popper:numeral $head $tail) $tail)))

      ; element([X|_], X).
      (rm-block member-here popper:synthesis-finddupl:bk-member-here
        (quote
          (popper:list:proof:member-here
            popper:numeral $head $tail))
        rm-nil
        (quote
          (popper:list:member popper:numeral
            (list:cons popper:numeral $head $tail) $head)))

      ; element([_|T], X) :- element(T, X).
      (rm-block member-there popper:synthesis-finddupl:bk-member-there
        (quote
          (popper:list:proof:member-there
            popper:numeral $head $tail $member
            (unquote $tail-proof)))
        (rm-cons
          (rm-premise $tail-proof
            (quote
              (popper:list:member
                popper:numeral $tail $member)))
          rm-nil)
        (quote
          (popper:list:member popper:numeral
            (list:cons popper:numeral $head $tail) $member)))

      ; f(V0,V1) :- tail(V0,V2), element(V2,V1), head(V0,V1).
      (rm-block direct popper:synthesis-finddupl:learned-direct
        (quote
          (finddupl-proof:direct
            $input $tail $duplicate
            (unquote $tail-proof)
            (unquote $member-proof)
            (unquote $head-proof)))
        (rm-cons
          (rm-premise $tail-proof
            (quote
              (popper:list:tail popper:numeral $input $tail)))
          (rm-cons
            (rm-premise $member-proof
              (quote
                (popper:list:member
                  popper:numeral $tail $duplicate)))
            (rm-cons
              (rm-premise $head-proof
                (quote
                  (popper:list:head
                    popper:numeral $input $duplicate)))
              rm-nil)))
        (quote (finddupl:target $input $duplicate)))

      ; f(V0,V1) :- tail(V0,V2), f(V2,V1).
      (rm-block recurse popper:synthesis-finddupl:learned-recurse
        (quote
          (finddupl-proof:recurse
            $input $tail $duplicate
            (unquote $tail-proof)
            (unquote $recursive-proof)))
        (rm-cons
          (rm-premise $tail-proof
            (quote
              (popper:list:tail popper:numeral $input $tail)))
          (rm-cons
            (rm-premise $recursive-proof
              (quote (finddupl:target $tail $duplicate)))
            rm-nil))
        (quote (finddupl:target $input $duplicate))))))
