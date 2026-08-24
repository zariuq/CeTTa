; Popper's authored synthesis-reverse program and its complete structural
; background, represented as ordinary proof-producing rules.
;
; The RuleMachine is an authority-free relational producer.  Recursive proof
; premises enter their parent constructor only through explicit staging; a
; complete occurrence must still inhabit its exact Prime index.

(= (popper:reverse:package)
  (compile:rule-package popper-synthesis-reverse-v1
    (rm-package
      (rm-block empty popper:synthesis-reverse:bk-empty
        (quote (popper:list:proof:empty reverse:element))
        rm-nil
        (quote
          (popper:list:empty reverse:element
            (list:nil reverse:element))))
      (rm-block head popper:synthesis-reverse:bk-head
        (quote
          (popper:list:proof:head reverse:element $head $tail))
        rm-nil
        (quote
          (popper:list:head reverse:element
            (list:cons reverse:element $head $tail) $head)))
      (rm-block tail popper:synthesis-reverse:bk-tail
        (quote
          (popper:list:proof:tail reverse:element $head $tail))
        rm-nil
        (quote
          (popper:list:tail reverse:element
            (list:cons reverse:element $head $tail) $tail)))

      ; my_append([], X, [X]).
      (rm-block append-last-empty
        popper:synthesis-reverse:bk-append-last-empty
        (quote
          (popper:list:proof:append-last-empty
            reverse:element $last))
        rm-nil
        (quote
          (popper:list:append-last reverse:element
            (list:nil reverse:element)
            $last
            (list:cons reverse:element $last
              (list:nil reverse:element)))))

      ; my_append([H|T], X, [H|R]) :- my_append(T, X, R).
      (rm-block append-last-step
        popper:synthesis-reverse:bk-append-last-step
        (quote
          (popper:list:proof:append-last-step
            reverse:element $head $tail $last $tail-result
            (unquote $tail-proof)))
        (rm-cons
          (rm-premise $tail-proof
            (quote
              (popper:list:append-last
                reverse:element $tail $last $tail-result)))
          rm-nil)
        (quote
          (popper:list:append-last reverse:element
            (list:cons reverse:element $head $tail)
            $last
            (list:cons reverse:element $head $tail-result))))

      ; f(V0,V1) :- empty(V1), empty(V0).
      (rm-block base popper:synthesis-reverse:learned-base
        (quote
          (reverse-proof:base
            $input $output
            (unquote $output-empty)
            (unquote $input-empty)))
        (rm-cons
          (rm-premise $output-empty
            (quote
              (popper:list:empty reverse:element $output)))
          (rm-cons
            (rm-premise $input-empty
              (quote
                (popper:list:empty reverse:element $input)))
            rm-nil))
        (quote (reverse:target $input $output)))

      ; f(V0,V1) :- head(V0,V4), tail(V0,V3), f(V3,V2),
      ;             my_append(V2,V4,V1).
      (rm-block step popper:synthesis-reverse:learned-step
        (quote
          (reverse-proof:step
            $input $head $tail $tail-reversed $output
            (unquote $head-proof)
            (unquote $tail-proof)
            (unquote $recursive-proof)
            (unquote $append-proof)))
        (rm-cons
          (rm-premise $head-proof
            (quote
              (popper:list:head
                reverse:element $input $head)))
          (rm-cons
            (rm-premise $tail-proof
              (quote
                (popper:list:tail
                  reverse:element $input $tail)))
            (rm-cons
              (rm-premise $recursive-proof
                (quote (reverse:target $tail $tail-reversed)))
              (rm-cons
                (rm-premise $append-proof
                  (quote
                    (popper:list:append-last
                      reverse:element
                      $tail-reversed $head $output)))
                rm-nil))))
        (quote (reverse:target $input $output))))))
