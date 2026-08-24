; The authored best program and background relation for Popper's
; synthesis-length task, represented as ordinary proof-producing rules.
;
; Proof templates are syntax data.  A premise proof crosses into its parent
; constructor only through an explicit `unquote (quote proof)` splice.  The
; RuleMachine remains an authority-free producer; Prime checks complete
; occurrences at its typed ingress.

(= (popper:length:package)
  (compile:rule-package popper-synthesis-length-v1
    (rm-package
      (rm-block empty popper:synthesis-length:bk-empty
        (quote length-proof:empty)
        rm-nil
        (quote (bk:empty (list:nil element))))
      (rm-block tail popper:synthesis-length:bk-tail
        (quote (length-proof:tail $head $tail))
        rm-nil
        (quote
          (bk:tail (list:cons element $head $tail) $tail)))
      (rm-block zero popper:synthesis-length:bk-zero
        (quote length-proof:zero)
        rm-nil
        (quote (bk:zero n0)))
      (rm-block succ-0-1 popper:synthesis-length:bk-succ-0-1
        (quote length-proof:succ-0-1) rm-nil (quote (bk:succ n0 n1)))
      (rm-block succ-1-2 popper:synthesis-length:bk-succ-1-2
        (quote length-proof:succ-1-2) rm-nil (quote (bk:succ n1 n2)))
      (rm-block succ-2-3 popper:synthesis-length:bk-succ-2-3
        (quote length-proof:succ-2-3) rm-nil (quote (bk:succ n2 n3)))
      (rm-block succ-3-4 popper:synthesis-length:bk-succ-3-4
        (quote length-proof:succ-3-4) rm-nil (quote (bk:succ n3 n4)))
      (rm-block succ-4-5 popper:synthesis-length:bk-succ-4-5
        (quote length-proof:succ-4-5) rm-nil (quote (bk:succ n4 n5)))
      (rm-block succ-5-6 popper:synthesis-length:bk-succ-5-6
        (quote length-proof:succ-5-6) rm-nil (quote (bk:succ n5 n6)))
      (rm-block succ-6-7 popper:synthesis-length:bk-succ-6-7
        (quote length-proof:succ-6-7) rm-nil (quote (bk:succ n6 n7)))
      (rm-block succ-7-8 popper:synthesis-length:bk-succ-7-8
        (quote length-proof:succ-7-8) rm-nil (quote (bk:succ n7 n8)))
      (rm-block succ-8-9 popper:synthesis-length:bk-succ-8-9
        (quote length-proof:succ-8-9) rm-nil (quote (bk:succ n8 n9)))
      (rm-block succ-9-10 popper:synthesis-length:bk-succ-9-10
        (quote length-proof:succ-9-10) rm-nil (quote (bk:succ n9 n10)))

      ; f(A,B) :- zero(B), empty(A).
      (rm-block base popper:synthesis-length:learned-base
        (quote
          (length-proof:base
            $list $length
            (unquote $zero-proof)
            (unquote $empty-proof)))
        (rm-cons
          (rm-premise $zero-proof (quote (bk:zero $length)))
          (rm-cons
            (rm-premise $empty-proof (quote (bk:empty $list)))
            rm-nil))
        (quote (target:length $list $length)))

      ; f(A,B) :- tail(A,C), f(C,D), succ(D,B).
      (rm-block step popper:synthesis-length:learned-step
        (quote
          (length-proof:step
            $list $tail $previous $length
            (unquote $tail-proof)
            (unquote $recursive-proof)
            (unquote $successor-proof)))
        (rm-cons
          (rm-premise $tail-proof (quote (bk:tail $list $tail)))
          (rm-cons
            (rm-premise $recursive-proof
              (quote (target:length $tail $previous)))
            (rm-cons
              (rm-premise $successor-proof
                (quote (bk:succ $previous $length)))
              rm-nil)))
        (quote (target:length $list $length))))))
