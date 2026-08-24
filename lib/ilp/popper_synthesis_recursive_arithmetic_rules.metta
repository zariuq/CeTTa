; Proof-producing recursive arithmetic programs.
; The rule machine searches; Prime checks every emitted proof.

(= (popper:recursive-arithmetic:package)
  (compile:rule-package popper-recursive-arithmetic-v1
    (rm-package
      (rm-block list-empty popper:recursive:list-empty
        (quote (rel:list:empty-proof $element))
        rm-nil
        (quote (rel:list:empty $element (list:nil $element))))
      (rm-block list-head popper:recursive:list-head
        (quote (rel:list:head-proof $element $head $tail))
        rm-nil
        (quote (rel:list:head $element
          (list:cons $element $head $tail) $head)))
      (rm-block list-tail popper:recursive:list-tail
        (quote (rel:list:tail-proof $element $head $tail))
        rm-nil
        (quote (rel:list:tail $element
          (list:cons $element $head $tail) $tail)))
      (rm-block even-zero popper:recursive:even-zero
        (quote popper:nat:even:zero)
        rm-nil
        (quote (popper:nat:even popper:nat:zero)))
      (rm-block even-step popper:recursive:even-step
        (quote (popper:nat:even:step
          $before (unquote $before-evidence)))
        (rm-cons
          (rm-premise $before-evidence
            (quote (popper:nat:even $before)))
          rm-nil)
        (quote (popper:nat:even
          (popper:nat:succ (popper:nat:succ $before)))))
      (rm-block one popper:recursive:one
        (quote popper:nat:one:evidence)
        rm-nil
        (quote (popper:nat:one
          (popper:nat:succ popper:nat:zero))))
      (rm-block decrement popper:recursive:decrement
        (quote (popper:nat:decrement:evidence $previous))
        rm-nil
        (quote (popper:nat:decrement
          (popper:nat:succ $previous) $previous)))
      (rm-block all-even-empty popper:recursive:all-even-empty
        (quote (popper:all-even:proof:empty
          (unquote $empty-evidence)))
        (rm-cons
          (rm-premise $empty-evidence
            (quote (rel:list:empty popper:nat
              (list:nil popper:nat))))
          rm-nil)
        (quote (popper:all-even:f (list:nil popper:nat))))
      (rm-block all-even-step popper:recursive:all-even-step
        (quote (popper:all-even:proof:step $head $tail
          (unquote $head-evidence) (unquote $tail-evidence)
          (unquote $even-evidence)
          (unquote $recursive-evidence)))
        (rm-cons
          (rm-premise $head-evidence
            (quote (rel:list:head popper:nat
              (list:cons popper:nat $head $tail) $head)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (rel:list:tail popper:nat
                (list:cons popper:nat $head $tail) $tail)))
            (rm-cons
              (rm-premise $even-evidence
                (quote (popper:nat:even $head)))
              (rm-cons
                (rm-premise $recursive-evidence
                  (quote (popper:all-even:f $tail)))
                rm-nil))))
        (quote (popper:all-even:f
          (list:cons popper:nat $head $tail))))
      (rm-block drop-k-one popper:recursive:drop-k-one
        (quote (popper:drop-k:proof:one $source $result
          (unquote $one-evidence) (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $one-evidence
            (quote (popper:nat:one
              (popper:nat:succ popper:nat:zero))))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (rel:list:tail popper:nat $source $result)))
            rm-nil))
        (quote (popper:drop-k:f $source
          (popper:nat:succ popper:nat:zero) $result)))
      (rm-block drop-k-step popper:recursive:drop-k-step
        (quote (popper:drop-k:proof:step
          $source $amount $previous $intermediate $result
          (unquote $decrement-evidence)
          (unquote $recursive-evidence)
          (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $decrement-evidence
            (quote (popper:nat:decrement $amount $previous)))
          (rm-cons
            (rm-premise $recursive-evidence
              (quote (popper:drop-k:f
                $source $previous $intermediate)))
            (rm-cons
              (rm-premise $tail-evidence
                (quote (rel:list:tail popper:nat
                  $intermediate $result)))
              rm-nil)))
        (quote (popper:drop-k:f $source $amount $result)))
    )))
