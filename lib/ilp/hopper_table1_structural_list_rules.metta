; Proof-producing structural-list Hopper realizations.
; Branch, front, last, reverse, and snoc evidence stays explicit.

(= (hopper:table1:structural-list:package)
  (compile:rule-package hopper-table1-structural-list-v1
    (rm-package
      (rm-block front-singleton hopper:struct:front-singleton
        (quote (rel:list:front:singleton $element $head))
        rm-nil
        (quote (rel:list:front $element
          (list:cons $element $head (list:nil $element))
          (list:nil $element))))
      (rm-block front-cons hopper:struct:front-cons
        (quote (rel:list:front:cons $element $head $next $tail
          $front-tail (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $tail-evidence
            (quote (rel:list:front $element
              (list:cons $element $next $tail) $front-tail)))
          rm-nil)
        (quote (rel:list:front $element
          (list:cons $element $head
            (list:cons $element $next $tail))
          (list:cons $element $head $front-tail))))
      (rm-block last-singleton hopper:struct:last-singleton
        (quote (rel:list:last:singleton $element $last))
        rm-nil
        (quote (rel:list:last $element
          (list:cons $element $last (list:nil $element)) $last)))
      (rm-block last-cons hopper:struct:last-cons
        (quote (rel:list:last:cons $element $head $next $tail
          $last (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $tail-evidence
            (quote (rel:list:last $element
              (list:cons $element $next $tail) $last)))
          rm-nil)
        (quote (rel:list:last $element
          (list:cons $element $head
            (list:cons $element $next $tail)) $last)))
      (rm-block snoc-nil hopper:struct:snoc-nil
        (quote (rel:list:snoc:nil $element $last))
        rm-nil
        (quote (rel:list:snoc $element (list:nil $element) $last
          (list:cons $element $last (list:nil $element)))))
      (rm-block snoc-cons hopper:struct:snoc-cons
        (quote (rel:list:snoc:cons $element $head $tail $last
          $result-tail (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $tail-evidence
            (quote (rel:list:snoc $element $tail $last $result-tail)))
          rm-nil)
        (quote (rel:list:snoc $element
          (list:cons $element $head $tail) $last
          (list:cons $element $head $result-tail))))
      (rm-block fold-nil hopper:struct:fold-nil
        (quote (rel:fold:nil $element $accumulator $step $before))
        rm-nil
        (quote (rel:fold $element $accumulator $step $before
          (list:nil $element) $before)))
      (rm-block fold-cons hopper:struct:fold-cons
        (quote (rel:fold:cons $element $accumulator $step
          $before $head $tail $next $after
          (unquote $step-evidence) (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $step-evidence
            (quote ($step $before $head $next)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (rel:fold $element $accumulator $step
                $next $tail $after)))
            rm-nil))
        (quote (rel:fold $element $accumulator $step $before
          (list:cons $element $head $tail) $after)))
      (rm-block reverse-step hopper:struct:reverse-step
        (quote (hopper:reverse:step-proof $before $head
          (rel:list:head-proof hopper:atom $head $before)
          (rel:list:tail-proof hopper:atom $head $before)))
        rm-nil
        (quote (hopper:reverse:step $before $head
          (list:cons hopper:atom $head $before))))
      (rm-block reverse hopper:struct:reverse
        (quote (hopper:reverse:proof $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:fold hopper:atom (list hopper:atom)
              hopper:reverse:step (list:nil hopper:atom)
              $source $target)))
          rm-nil)
        (quote (hopper:reverse:f $source $target)))
      (rm-block nat-zero hopper:struct:nat-zero
        (quote hopper:nat:proof:zero)
        rm-nil
        (quote (hopper:nat:zero hopper:nat:n0)))
      (rm-block nat-succ-0-1 hopper:struct:nat-succ-0-1
        (quote hopper:nat:proof:succ-0-1)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n0 hopper:nat:n1)))
      (rm-block nat-succ-1-2 hopper:struct:nat-succ-1-2
        (quote hopper:nat:proof:succ-1-2)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n1 hopper:nat:n2)))
      (rm-block either-left hopper:struct:either-left
        (quote (rel:either:left $value $left $right $subject
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence (quote ($left $subject)))
          rm-nil)
        (quote (rel:either $value $left $right $subject)))
      (rm-block either-right hopper:struct:either-right
        (quote (rel:either:right $value $left $right $subject
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence (quote ($right $subject)))
          rm-nil)
        (quote (rel:either $value $left $right $subject)))
      (rm-block last-half-nil hopper:struct:last-half-nil
        (quote hopper:last-half:case:nil)
        rm-nil
        (quote (hopper:last-half:case
          (list:nil hopper:atom) (list:nil hopper:atom))))
      (rm-block last-half-singleton hopper:struct:last-half-singleton
        (quote (hopper:last-half:case:singleton $head))
        rm-nil
        (quote (hopper:last-half:case
          (list:cons hopper:atom $head (list:nil hopper:atom))
          (list:nil hopper:atom))))
      (rm-block last-half-cons hopper:struct:last-half-cons
        (quote (hopper:last-half:case:cons $head $next $rest
          $trimmed $recursive-result $target
          (unquote $front-evidence)
          (unquote $recursive-evidence)
          (unquote $snoc-evidence)))
        (rm-cons
          (rm-premise $front-evidence
            (quote (rel:list:front hopper:atom
              (list:cons hopper:atom $next $rest) $trimmed)))
          (rm-cons
            (rm-premise $recursive-evidence
              (quote (hopper:last-half:case
                $trimmed $recursive-result)))
            (rm-cons
              (rm-premise $snoc-evidence
                (quote (rel:list:snoc hopper:atom
                  $recursive-result $head $target)))
              rm-nil)))
        (quote (hopper:last-half:case
          (list:cons hopper:atom $head
            (list:cons hopper:atom $next $rest)) $target)))
      (rm-block target-last-half hopper:struct:target-last-half
        (quote (hopper:last-half:proof $source $reversed $target
          (unquote $reverse-evidence) (unquote $case-evidence)))
        (rm-cons
          (rm-premise $reverse-evidence
            (quote (hopper:reverse:f $source $reversed)))
          (rm-cons
            (rm-premise $case-evidence
              (quote (hopper:last-half:case $reversed $target)))
            rm-nil))
        (quote (hopper:last-half:f $source $target)))
      (rm-block one hopper:struct:one
        (quote (hopper:one:proof
          (unquote $zero-evidence) (unquote $successor-evidence)))
        (rm-cons
          (rm-premise $zero-evidence
            (quote (hopper:nat:zero hopper:nat:n0)))
          (rm-cons
            (rm-premise $successor-evidence
              (quote (hopper:nat:succ hopper:nat:n0 hopper:nat:n1)))
            rm-nil))
        (quote (hopper:one hopper:nat:n1)))
      (rm-block two hopper:struct:two
        (quote (hopper:two:proof (unquote $zero-evidence)
          (unquote $first-successor)
          (unquote $second-successor)))
        (rm-cons
          (rm-premise $zero-evidence
            (quote (hopper:nat:zero hopper:nat:n0)))
          (rm-cons
            (rm-premise $first-successor
              (quote (hopper:nat:succ hopper:nat:n0 hopper:nat:n1)))
            (rm-cons
              (rm-premise $second-successor
                (quote (hopper:nat:succ hopper:nat:n1 hopper:nat:n2)))
              rm-nil)))
        (quote (hopper:two hopper:nat:n2)))
      (rm-block of-one-and-two-nil hopper:struct:of-one-and-two-nil
        (quote hopper:of-one-and-two:nil)
        rm-nil
        (quote (hopper:of-one-and-two:f (list:nil hopper:nat))))
      (rm-block of-one-and-two-cons hopper:struct:of-one-and-two-cons
        (quote (hopper:of-one-and-two:cons $head $tail
          (unquote $choice-evidence) (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $choice-evidence
            (quote (rel:either hopper:nat hopper:one hopper:two $head)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (hopper:of-one-and-two:f $tail)))
            rm-nil))
        (quote (hopper:of-one-and-two:f
          (list:cons hopper:nat $head $tail))))
      (rm-block palindrome-nil hopper:struct:palindrome-nil
        (quote hopper:palindrome:nil)
        rm-nil
        (quote (hopper:palindrome:f (list:nil hopper:atom))))
      (rm-block palindrome-singleton hopper:struct:palindrome-singleton
        (quote (hopper:palindrome:singleton $head))
        rm-nil
        (quote (hopper:palindrome:f
          (list:cons hopper:atom $head (list:nil hopper:atom)))))
      (rm-block palindrome-cons hopper:struct:palindrome-cons
        (quote (hopper:palindrome:cons $head $next $rest $middle
          (unquote $last-evidence) (unquote $front-evidence)
          (unquote $recursive-evidence)))
        (rm-cons
          (rm-premise $last-evidence
            (quote (rel:list:last hopper:atom
              (list:cons hopper:atom $next $rest) $head)))
          (rm-cons
            (rm-premise $front-evidence
              (quote (rel:list:front hopper:atom
                (list:cons hopper:atom $next $rest) $middle)))
            (rm-cons
              (rm-premise $recursive-evidence
                (quote (hopper:palindrome:f $middle)))
              rm-nil)))
        (quote (hopper:palindrome:f
          (list:cons hopper:atom $head
            (list:cons hopper:atom $next $rest)))))
    )))
