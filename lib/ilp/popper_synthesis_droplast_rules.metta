; Popper's complete synthesis-droplast structural background and authored
; two-clause program, represented as ordinary proof-producing rules.

(= (popper:droplast:package)
  (compile:rule-package popper-synthesis-droplast-v1
    (rm-package
      (rm-block empty popper:synthesis-droplast:bk-empty
        (quote (popper:list:proof:empty popper:numeral))
        rm-nil
        (quote
          (popper:list:empty popper:numeral
            (list:nil popper:numeral))))
      (rm-block head popper:synthesis-droplast:bk-head
        (quote
          (popper:list:proof:head popper:numeral $head $tail))
        rm-nil
        (quote
          (popper:list:head popper:numeral
            (list:cons popper:numeral $head $tail) $head)))
      (rm-block tail popper:synthesis-droplast:bk-tail
        (quote
          (popper:list:proof:tail popper:numeral $head $tail))
        rm-nil
        (quote
          (popper:list:tail popper:numeral
            (list:cons popper:numeral $head $tail) $tail)))
      (rm-block prepend popper:synthesis-droplast:bk-prepend
        (quote
          (popper:list:proof:prepend
            popper:numeral $head $tail))
        rm-nil
        (quote
          (popper:list:prepend popper:numeral $head $tail
            (list:cons popper:numeral $head $tail))))

      ; f(V0,V1) :- empty(V1), tail(V0,V1).
      (rm-block base popper:synthesis-droplast:learned-base
        (quote
          (droplast-proof:base
            $input $output
            (unquote $output-empty)
            (unquote $tail-proof)))
        (rm-cons
          (rm-premise $output-empty
            (quote
              (popper:list:empty popper:numeral $output)))
          (rm-cons
            (rm-premise $tail-proof
              (quote
                (popper:list:tail
                  popper:numeral $input $output)))
            rm-nil))
        (quote (droplast:target $input $output)))

      ; f(V0,V1) :- head(V0,V3), tail(V0,V4), f(V4,V2),
      ;             append_(V3,V2,V1).
      (rm-block step popper:synthesis-droplast:learned-step
        (quote
          (droplast-proof:step
            $input $head $tail $tail-output $output
            (unquote $head-proof)
            (unquote $tail-proof)
            (unquote $recursive-proof)
            (unquote $prepend-proof)))
        (rm-cons
          (rm-premise $head-proof
            (quote
              (popper:list:head popper:numeral $input $head)))
          (rm-cons
            (rm-premise $tail-proof
              (quote
                (popper:list:tail popper:numeral $input $tail)))
            (rm-cons
              (rm-premise $recursive-proof
                (quote (droplast:target $tail $tail-output)))
              (rm-cons
                (rm-premise $prepend-proof
                  (quote
                    (popper:list:prepend popper:numeral
                      $head $tail-output $output)))
                rm-nil))))
        (quote (droplast:target $input $output))))))
