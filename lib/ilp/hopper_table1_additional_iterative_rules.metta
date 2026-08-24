; Proof-producing realization of three authored iterative programs.
; Shared iterate/map-rel/snoc evidence remains visible in every result.

(= (hopper:table1:additional-iterative:package)
  (compile:rule-package hopper-table1-additional-iterative-v1
    (rm-package
      (rm-block list-head hopper:iter:list-head
        (quote (rel:list:head-proof $element $head $tail))
        rm-nil
        (quote (rel:list:head $element
          (list:cons $element $head $tail) $head)))
      (rm-block list-tail hopper:iter:list-tail
        (quote (rel:list:tail-proof $element $head $tail))
        rm-nil
        (quote (rel:list:tail $element
          (list:cons $element $head $tail) $tail)))
      (rm-block snoc-nil hopper:iter:snoc-nil
        (quote (rel:list:snoc:nil $element $last))
        rm-nil
        (quote (rel:list:snoc $element (list:nil $element) $last
          (list:cons $element $last (list:nil $element)))))
      (rm-block snoc-cons hopper:iter:snoc-cons
        (quote (rel:list:snoc:cons $element $head $tail $last
          $result-tail (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $tail-evidence
            (quote (rel:list:snoc $element $tail $last $result-tail)))
          rm-nil)
        (quote (rel:list:snoc $element
          (list:cons $element $head $tail) $last
          (list:cons $element $head $result-tail))))
      (rm-block nat-succ-0-1 hopper:iter:nat-succ-0-1
        (quote hopper:nat:proof:succ-0-1)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n0 hopper:nat:n1)))
      (rm-block nat-succ-1-2 hopper:iter:nat-succ-1-2
        (quote hopper:nat:proof:succ-1-2)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n1 hopper:nat:n2)))
      (rm-block nat-succ-2-3 hopper:iter:nat-succ-2-3
        (quote hopper:nat:proof:succ-2-3)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n2 hopper:nat:n3)))
      (rm-block nat-succ-3-4 hopper:iter:nat-succ-3-4
        (quote hopper:nat:proof:succ-3-4)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n3 hopper:nat:n4)))
      (rm-block nat-succ-4-5 hopper:iter:nat-succ-4-5
        (quote hopper:nat:proof:succ-4-5)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n4 hopper:nat:n5)))
      (rm-block nat-succ-5-6 hopper:iter:nat-succ-5-6
        (quote hopper:nat:proof:succ-5-6)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n5 hopper:nat:n6)))
      (rm-block nat-succ-6-7 hopper:iter:nat-succ-6-7
        (quote hopper:nat:proof:succ-6-7)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n6 hopper:nat:n7)))
      (rm-block nat-succ-7-8 hopper:iter:nat-succ-7-8
        (quote hopper:nat:proof:succ-7-8)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n7 hopper:nat:n8)))
      (rm-block nat-succ-8-9 hopper:iter:nat-succ-8-9
        (quote hopper:nat:proof:succ-8-9)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n8 hopper:nat:n9)))
      (rm-block nat-succ-9-10 hopper:iter:nat-succ-9-10
        (quote hopper:nat:proof:succ-9-10)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n9 hopper:nat:n10)))
      (rm-block nat-succ-10-11 hopper:iter:nat-succ-10-11
        (quote hopper:nat:proof:succ-10-11)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n10 hopper:nat:n11)))
      (rm-block nat-succ-11-12 hopper:iter:nat-succ-11-12
        (quote hopper:nat:proof:succ-11-12)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n11 hopper:nat:n12)))
      (rm-block nat-succ-12-13 hopper:iter:nat-succ-12-13
        (quote hopper:nat:proof:succ-12-13)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n12 hopper:nat:n13)))
      (rm-block nat-succ-13-14 hopper:iter:nat-succ-13-14
        (quote hopper:nat:proof:succ-13-14)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n13 hopper:nat:n14)))
      (rm-block nat-succ-14-15 hopper:iter:nat-succ-14-15
        (quote hopper:nat:proof:succ-14-15)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n14 hopper:nat:n15)))
      (rm-block nat-succ-15-16 hopper:iter:nat-succ-15-16
        (quote hopper:nat:proof:succ-15-16)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n15 hopper:nat:n16)))
      (rm-block nat-succ-16-17 hopper:iter:nat-succ-16-17
        (quote hopper:nat:proof:succ-16-17)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n16 hopper:nat:n17)))
      (rm-block nat-pred hopper:iter:nat-pred
        (quote (hopper:nat:proof:pred-from-succ
          $later $earlier (unquote $successor-evidence)))
        (rm-cons
          (rm-premise $successor-evidence
            (quote (hopper:nat:succ $earlier $later)))
          rm-nil)
        (quote (hopper:nat:pred $later $earlier)))
      (rm-block iterate-zero hopper:iter:iterate-zero
        (quote (rel:iterate:zero $value $counter $step
          $predecessor $zero $source))
        rm-nil
        (quote (rel:iterate $value $counter $step $predecessor
          $zero $source $zero $source)))
      (rm-block iterate-step hopper:iter:iterate-step
        (quote (rel:iterate:step $value $counter $step
          $predecessor $zero $source $next $later $earlier $target
          (unquote $predecessor-evidence)
          (unquote $step-evidence)
          (unquote $recursive-evidence)))
        (rm-cons
          (rm-premise $predecessor-evidence
            (quote ($predecessor $later $earlier)))
          (rm-cons
            (rm-premise $step-evidence
              (quote ($step $source $next)))
            (rm-cons
              (rm-premise $recursive-evidence
                (quote (rel:iterate $value $counter $step
                  $predecessor $zero $next $earlier $target)))
              rm-nil)))
        (quote (rel:iterate $value $counter $step $predecessor
          $zero $source $later $target)))
      (rm-block map-rel-nil hopper:iter:map-rel-nil
        (quote (map-rel:nil $source $target $relation))
        rm-nil
        (quote (map-rel $source $target $relation
          (list:nil $source) (list:nil $target))))
      (rm-block map-rel-cons hopper:iter:map-rel-cons
        (quote (map-rel:cons $source $target $relation
          $source-head $target-head $source-tail $target-tail
          (unquote $head-evidence) (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $head-evidence
            (quote ($relation $source-head $target-head)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (map-rel $source $target $relation
                $source-tail $target-tail)))
            rm-nil))
        (quote (map-rel $source $target $relation
          (list:cons $source $source-head $source-tail)
          (list:cons $target $target-head $target-tail))))
      (rm-block fold-nil hopper:iter:fold-nil
        (quote (rel:fold:nil $element $accumulator $step $before))
        rm-nil
        (quote (rel:fold $element $accumulator $step $before
          (list:nil $element) $before)))
      (rm-block fold-cons hopper:iter:fold-cons
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
      (rm-block reverse-step hopper:iter:reverse-step
        (quote (hopper:reverse:step-proof $before $head
          (rel:list:head-proof hopper:atom $head $before)
          (rel:list:tail-proof hopper:atom $head $before)))
        rm-nil
        (quote (hopper:reverse:step $before $head
          (list:cons hopper:atom $head $before))))
      (rm-block reverse hopper:iter:reverse
        (quote (hopper:reverse:proof $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:fold hopper:atom (list hopper:atom)
              hopper:reverse:step (list:nil hopper:atom)
              $source $target)))
          rm-nil)
        (quote (hopper:reverse:f $source $target)))
      (rm-block drop-last-element hopper:iter:drop-last-element
        (quote (hopper:drop-last:element-proof
          $source $reversed $trimmed $target
          (unquote $source-reversal) (unquote $tail-evidence)
          (unquote $target-reversal)))
        (rm-cons
          (rm-premise $source-reversal
            (quote (hopper:reverse:f $source $reversed)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (rel:list:tail hopper:atom $reversed $trimmed)))
            (rm-cons
              (rm-premise $target-reversal
                (quote (hopper:reverse:f $trimmed $target)))
              rm-nil)))
        (quote (hopper:drop-last:element $source $target)))
      (rm-block rotate-step hopper:iter:rotate-step
        (quote (hopper:rotate-n:step-proof
          $source $head $tail $target
          (unquote $head-evidence) (unquote $tail-evidence)
          (unquote $snoc-evidence)))
        (rm-cons
          (rm-premise $head-evidence
            (quote (rel:list:head hopper:nat $source $head)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (rel:list:tail hopper:nat $source $tail)))
            (rm-cons
              (rm-premise $snoc-evidence
                (quote (rel:list:snoc hopper:nat $tail $head $target)))
              rm-nil)))
        (quote (hopper:rotate-n:step $source $target)))
      (rm-block drop-last-k-step hopper:iter:drop-last-k-step
        (quote (hopper:drop-last-k:step-proof $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (map-rel (list hopper:atom) (list hopper:atom)
              hopper:drop-last:element $source $target)))
          rm-nil)
        (quote (hopper:drop-last-k:step $source $target)))
      (rm-block add-n-step hopper:iter:add-n-step
        (quote (hopper:add-n:step-proof $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (map-rel hopper:nat hopper:nat hopper:nat:succ
              $source $target)))
          rm-nil)
        (quote (hopper:add-n:step $source $target)))
      (rm-block target-rotate hopper:iter:target-rotate
        (quote (hopper:rotate-n:proof $count $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:iterate (list hopper:nat) hopper:nat
              hopper:rotate-n:step hopper:nat:pred hopper:nat:n0
              $source $count $target)))
          rm-nil)
        (quote (hopper:rotate-n:f $count $source $target)))
      (rm-block target-drop-last-k hopper:iter:target-drop-last-k
        (quote (hopper:drop-last-k:proof $count $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:iterate (list (list hopper:atom)) hopper:nat
              hopper:drop-last-k:step hopper:nat:pred hopper:nat:n0
              $source $count $target)))
          rm-nil)
        (quote (hopper:drop-last-k:f $count $source $target)))
      (rm-block target-add-n hopper:iter:target-add-n
        (quote (hopper:add-n:proof $count $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:iterate (list hopper:nat) hopper:nat
              hopper:add-n:step hopper:nat:pred hopper:nat:n0
              $source $count $target)))
          rm-nil)
        (quote (hopper:add-n:f $count $source $target)))
    )))
