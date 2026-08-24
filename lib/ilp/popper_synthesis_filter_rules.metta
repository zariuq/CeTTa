; Popper's complete synthesis-filter background and authored three-clause
; program, represented as ordinary proof-producing relational rules.

(= (popper:filter:package)
  (compile:rule-package popper-synthesis-filter-v1
    (rm-package
      (rm-block empty popper:synthesis-filter:bk-empty
        (quote (popper:list:proof:empty filter:element))
        rm-nil
        (quote
          (popper:list:empty filter:element
            (list:nil filter:element))))
      (rm-block head popper:synthesis-filter:bk-head
        (quote
          (popper:list:proof:head filter:element $head $tail))
        rm-nil
        (quote
          (popper:list:head filter:element
            (list:cons filter:element $head $tail) $head)))
      (rm-block tail popper:synthesis-filter:bk-tail
        (quote
          (popper:list:proof:tail filter:element $head $tail))
        rm-nil
        (quote
          (popper:list:tail filter:element
            (list:cons filter:element $head $tail) $tail)))
      (rm-block prepend popper:synthesis-filter:bk-prepend
        (quote
          (popper:list:proof:prepend
            filter:element $head $tail))
        rm-nil
        (quote
          (popper:list:prepend filter:element $head $tail
            (list:cons filter:element $head $tail))))

      (rm-block odd-1 popper:synthesis-filter:bk-odd-1
        (quote filter-proof:odd-1) rm-nil
        (quote (filter:odd filter:n1)))
      (rm-block odd-3 popper:synthesis-filter:bk-odd-3
        (quote filter-proof:odd-3) rm-nil
        (quote (filter:odd filter:n3)))
      (rm-block odd-5 popper:synthesis-filter:bk-odd-5
        (quote filter-proof:odd-5) rm-nil
        (quote (filter:odd filter:n5)))
      (rm-block odd-7 popper:synthesis-filter:bk-odd-7
        (quote filter-proof:odd-7) rm-nil
        (quote (filter:odd filter:n7)))
      (rm-block odd-9 popper:synthesis-filter:bk-odd-9
        (quote filter-proof:odd-9) rm-nil
        (quote (filter:odd filter:n9)))
      (rm-block even-2 popper:synthesis-filter:bk-even-2
        (quote filter-proof:even-2) rm-nil
        (quote (filter:even filter:n2)))
      (rm-block even-4 popper:synthesis-filter:bk-even-4
        (quote filter-proof:even-4) rm-nil
        (quote (filter:even filter:n4)))
      (rm-block even-6 popper:synthesis-filter:bk-even-6
        (quote filter-proof:even-6) rm-nil
        (quote (filter:even filter:n6)))
      (rm-block even-8 popper:synthesis-filter:bk-even-8
        (quote filter-proof:even-8) rm-nil
        (quote (filter:even filter:n8)))
      (rm-block even-10 popper:synthesis-filter:bk-even-10
        (quote filter-proof:even-10) rm-nil
        (quote (filter:even filter:n10)))

      ; f(V0,V1) :- empty(V0), empty(V1).
      (rm-block base popper:synthesis-filter:learned-base
        (quote
          (filter-proof:base
            $input $output
            (unquote $input-empty)
            (unquote $output-empty)))
        (rm-cons
          (rm-premise $input-empty
            (quote
              (popper:list:empty filter:element $input)))
          (rm-cons
            (rm-premise $output-empty
              (quote
                (popper:list:empty filter:element $output)))
            rm-nil))
        (quote (filter:target $input $output)))

      ; f(V0,V1) :- head(V0,V3), odd(V3), tail(V0,V2), f(V2,V1).
      (rm-block odd-step popper:synthesis-filter:learned-odd-step
        (quote
          (filter-proof:odd-step
            $input $head $tail $output
            (unquote $head-proof)
            (unquote $odd-proof)
            (unquote $tail-proof)
            (unquote $recursive-proof)))
        (rm-cons
          (rm-premise $head-proof
            (quote
              (popper:list:head
                filter:element $input $head)))
          (rm-cons
            (rm-premise $odd-proof
              (quote (filter:odd $head)))
            (rm-cons
              (rm-premise $tail-proof
                (quote
                  (popper:list:tail
                    filter:element $input $tail)))
              (rm-cons
                (rm-premise $recursive-proof
                  (quote (filter:target $tail $output)))
                rm-nil))))
        (quote (filter:target $input $output)))

      ; f(V0,V1) :- tail(V0,V2), head(V0,V3), even(V3), f(V2,V4),
      ;             prepend(V3,V4,V1).
      (rm-block even-step popper:synthesis-filter:learned-even-step
        (quote
          (filter-proof:even-step
            $input $head $tail $tail-output $output
            (unquote $tail-proof)
            (unquote $head-proof)
            (unquote $even-proof)
            (unquote $recursive-proof)
            (unquote $prepend-proof)))
        (rm-cons
          (rm-premise $tail-proof
            (quote
              (popper:list:tail
                filter:element $input $tail)))
          (rm-cons
            (rm-premise $head-proof
              (quote
                (popper:list:head
                  filter:element $input $head)))
            (rm-cons
              (rm-premise $even-proof
                (quote (filter:even $head)))
              (rm-cons
                (rm-premise $recursive-proof
                  (quote
                    (filter:target $tail $tail-output)))
                (rm-cons
                  (rm-premise $prepend-proof
                    (quote
                      (popper:list:prepend filter:element
                        $head $tail-output $output)))
                  rm-nil)))))
        (quote (filter:target $input $output))))))
