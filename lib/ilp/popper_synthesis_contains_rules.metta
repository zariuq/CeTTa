; Popper's authored synthesis-contains reference program and its exact
; reachable background slice, represented as proof-producing rules.

(= (popper:contains:package)
  (compile:rule-package popper-synthesis-contains-v1
    (rm-package
      (rm-block head popper:synthesis-contains:bk-head
        (quote
          (popper:list:proof:head popper:numeral $head $tail))
        rm-nil
        (quote
          (popper:list:head popper:numeral
            (list:cons popper:numeral $head $tail) $head)))
      (rm-block tail popper:synthesis-contains:bk-tail
        (quote
          (popper:list:proof:tail popper:numeral $head $tail))
        rm-nil
        (quote
          (popper:list:tail popper:numeral
            (list:cons popper:numeral $head $tail) $tail)))
      (rm-block c6 popper:synthesis-contains:bk-c6
        (quote contains-proof:c6)
        rm-nil
        (quote (contains:c6 popper:n6)))
      (rm-block c9 popper:synthesis-contains:bk-c9
        (quote contains-proof:c9)
        rm-nil
        (quote (contains:c9 popper:n9)))

      ; f(V0) :- head(V0,V1), c_6(V1).
      (rm-block at-6 popper:synthesis-contains:learned-at-6
        (quote
          (contains-proof:at-6
            $input $head
            (unquote $head-proof)
            (unquote $constant-proof)))
        (rm-cons
          (rm-premise $head-proof
            (quote
              (popper:list:head popper:numeral $input $head)))
          (rm-cons
            (rm-premise $constant-proof
              (quote (contains:c6 $head)))
            rm-nil))
        (quote (contains:target $input)))

      ; f(V0) :- head(V0,V1), c_9(V1).
      (rm-block at-9 popper:synthesis-contains:learned-at-9
        (quote
          (contains-proof:at-9
            $input $head
            (unquote $head-proof)
            (unquote $constant-proof)))
        (rm-cons
          (rm-premise $head-proof
            (quote
              (popper:list:head popper:numeral $input $head)))
          (rm-cons
            (rm-premise $constant-proof
              (quote (contains:c9 $head)))
            rm-nil))
        (quote (contains:target $input)))

      ; f(V0) :- tail(V0,V1), f(V1).
      (rm-block recurse popper:synthesis-contains:learned-recurse
        (quote
          (contains-proof:recurse
            $input $tail
            (unquote $tail-proof)
            (unquote $recursive-proof)))
        (rm-cons
          (rm-premise $tail-proof
            (quote
              (popper:list:tail popper:numeral $input $tail)))
          (rm-cons
            (rm-premise $recursive-proof
              (quote (contains:target $tail)))
            rm-nil))
        (quote (contains:target $input))))))
