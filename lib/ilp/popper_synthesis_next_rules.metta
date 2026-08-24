; Complete pinned background for synthesis-next plus the size-8 program
; learned by Popper from all seven positive and four negative examples.
; Body order is an explicit execution schedule; the learned logical clause is
; invariant under permutation of its four conjuncts.

(= (popper:next:package)
  (compile:rule-package popper-synthesis-next-v1
    (rm-package
      (rm-block head popper:synthesis-next:bk-head
        (quote
          (popper:list:proof:head next:element $head $tail))
        rm-nil
        (quote
          (popper:list:head next:element
            (list:cons next:element $head $tail) $head)))
      (rm-block tail popper:synthesis-next:bk-tail
        (quote
          (popper:list:proof:tail next:element $head $tail))
        rm-nil
        (quote
          (popper:list:tail next:element
            (list:cons next:element $head $tail) $tail)))

      (rm-block is-a popper:synthesis-next:bk-a
        (quote next-proof:is-a) rm-nil
        (quote (next:is-a next:a)))
      (rm-block is-b popper:synthesis-next:bk-b
        (quote next-proof:is-b) rm-nil
        (quote (next:is-b next:b)))
      (rm-block is-c popper:synthesis-next:bk-c
        (quote next-proof:is-c) rm-nil
        (quote (next:is-c next:c)))
      (rm-block is-d popper:synthesis-next:bk-d
        (quote next-proof:is-d) rm-nil
        (quote (next:is-d next:d)))
      (rm-block is-e popper:synthesis-next:bk-e
        (quote next-proof:is-e) rm-nil
        (quote (next:is-e next:e)))
      (rm-block is-f popper:synthesis-next:bk-f
        (quote next-proof:is-f) rm-nil
        (quote (next:is-f next:f)))
      (rm-block is-g popper:synthesis-next:bk-g
        (quote next-proof:is-g) rm-nil
        (quote (next:is-g next:g)))
      (rm-block is-h popper:synthesis-next:bk-h
        (quote next-proof:is-h) rm-nil
        (quote (next:is-h next:h)))
      (rm-block is-i popper:synthesis-next:bk-i
        (quote next-proof:is-i) rm-nil
        (quote (next:is-i next:i)))
      (rm-block is-j popper:synthesis-next:bk-j
        (quote next-proof:is-j) rm-nil
        (quote (next:is-j next:j)))
      (rm-block is-x popper:synthesis-next:bk-x
        (quote next-proof:is-x) rm-nil
        (quote (next:is-x next:x)))

      ; next_list(A,B) :- head(A,X), x(X), tail(A,T), head(T,B).
      (rm-block adjacent popper:synthesis-next:learned-adjacent
        (quote
          (next-proof:adjacent
            $input $marker $tail $following
            (unquote $marker-head)
            (unquote $marker-is-x)
            (unquote $tail-proof)
            (unquote $following-head)))
        (rm-cons
          (rm-premise $marker-head
            (quote
              (popper:list:head
                next:element $input $marker)))
          (rm-cons
            (rm-premise $marker-is-x
              (quote (next:is-x $marker)))
            (rm-cons
              (rm-premise $tail-proof
                (quote
                  (popper:list:tail
                    next:element $input $tail)))
              (rm-cons
                (rm-premise $following-head
                  (quote
                    (popper:list:head
                      next:element $tail $following)))
                rm-nil))))
        (quote (next:target $input $following)))

      ; next_list(A,B) :- tail(A,T), next_list(T,B).
      (rm-block scan popper:synthesis-next:learned-scan
        (quote
          (next-proof:scan
            $input $tail $following
            (unquote $tail-proof)
            (unquote $recursive-proof)))
        (rm-cons
          (rm-premise $tail-proof
            (quote
              (popper:list:tail
                next:element $input $tail)))
          (rm-cons
            (rm-premise $recursive-proof
              (quote (next:target $tail $following)))
            rm-nil))
        (quote (next:target $input $following))))))
