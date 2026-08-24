; Proof-producing execution of four authored recursive programs.
; Generic evidence retains environments, states, and every premise.

(= (hopper:table1:relational-recursion:package)
  (compile:rule-package hopper-table1-relational-recursion-v1
    (rm-package
      (rm-block list-head hopper:rec:list-head
        (quote (rel:list:head-proof $element $head $tail))
        rm-nil
        (quote (rel:list:head $element
          (list:cons $element $head $tail) $head)))
      (rm-block list-tail hopper:rec:list-tail
        (quote (rel:list:tail-proof $element $head $tail))
        rm-nil
        (quote (rel:list:tail $element
          (list:cons $element $head $tail) $tail)))
      (rm-block snoc-nil hopper:rec:snoc-nil
        (quote (rel:list:snoc:nil $element $last))
        rm-nil
        (quote (rel:list:snoc $element (list:nil $element) $last
          (list:cons $element $last (list:nil $element)))))
      (rm-block snoc-cons hopper:rec:snoc-cons
        (quote (rel:list:snoc:cons $element $head $tail $last
          $result-tail (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $tail-evidence
            (quote (rel:list:snoc $element $tail $last $result-tail)))
          rm-nil)
        (quote (rel:list:snoc $element
          (list:cons $element $head $tail) $last
          (list:cons $element $head $result-tail))))
      (rm-block nat-succ-0-1 hopper:rec:nat-succ-0-1
        (quote hopper:nat:proof:succ-0-1)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n0 hopper:nat:n1)))
      (rm-block nat-succ-1-2 hopper:rec:nat-succ-1-2
        (quote hopper:nat:proof:succ-1-2)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n1 hopper:nat:n2)))
      (rm-block nat-succ-2-3 hopper:rec:nat-succ-2-3
        (quote hopper:nat:proof:succ-2-3)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n2 hopper:nat:n3)))
      (rm-block nat-succ-3-4 hopper:rec:nat-succ-3-4
        (quote hopper:nat:proof:succ-3-4)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n3 hopper:nat:n4)))
      (rm-block nat-succ-4-5 hopper:rec:nat-succ-4-5
        (quote hopper:nat:proof:succ-4-5)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n4 hopper:nat:n5)))
      (rm-block nat-succ-5-6 hopper:rec:nat-succ-5-6
        (quote hopper:nat:proof:succ-5-6)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n5 hopper:nat:n6)))
      (rm-block nat-succ-6-7 hopper:rec:nat-succ-6-7
        (quote hopper:nat:proof:succ-6-7)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n6 hopper:nat:n7)))
      (rm-block nat-succ-7-8 hopper:rec:nat-succ-7-8
        (quote hopper:nat:proof:succ-7-8)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n7 hopper:nat:n8)))
      (rm-block nat-succ-8-9 hopper:rec:nat-succ-8-9
        (quote hopper:nat:proof:succ-8-9)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n8 hopper:nat:n9)))
      (rm-block nat-succ-9-10 hopper:rec:nat-succ-9-10
        (quote hopper:nat:proof:succ-9-10)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n9 hopper:nat:n10)))
      (rm-block nat-succ-10-11 hopper:rec:nat-succ-10-11
        (quote hopper:nat:proof:succ-10-11)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n10 hopper:nat:n11)))
      (rm-block nat-succ-11-12 hopper:rec:nat-succ-11-12
        (quote hopper:nat:proof:succ-11-12)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n11 hopper:nat:n12)))
      (rm-block nat-succ-12-13 hopper:rec:nat-succ-12-13
        (quote hopper:nat:proof:succ-12-13)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n12 hopper:nat:n13)))
      (rm-block nat-succ-13-14 hopper:rec:nat-succ-13-14
        (quote hopper:nat:proof:succ-13-14)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n13 hopper:nat:n14)))
      (rm-block nat-succ-14-15 hopper:rec:nat-succ-14-15
        (quote hopper:nat:proof:succ-14-15)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n14 hopper:nat:n15)))
      (rm-block nat-succ-15-16 hopper:rec:nat-succ-15-16
        (quote hopper:nat:proof:succ-15-16)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n15 hopper:nat:n16)))
      (rm-block nat-succ-16-17 hopper:rec:nat-succ-16-17
        (quote hopper:nat:proof:succ-16-17)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n16 hopper:nat:n17)))
      (rm-block nat-succ-17-18 hopper:rec:nat-succ-17-18
        (quote hopper:nat:proof:succ-17-18)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n17 hopper:nat:n18)))
      (rm-block nat-succ-18-19 hopper:rec:nat-succ-18-19
        (quote hopper:nat:proof:succ-18-19)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n18 hopper:nat:n19)))
      (rm-block nat-succ-19-20 hopper:rec:nat-succ-19-20
        (quote hopper:nat:proof:succ-19-20)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n19 hopper:nat:n20)))
      (rm-block nat-succ-20-21 hopper:rec:nat-succ-20-21
        (quote hopper:nat:proof:succ-20-21)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n20 hopper:nat:n21)))
      (rm-block nat-succ-21-22 hopper:rec:nat-succ-21-22
        (quote hopper:nat:proof:succ-21-22)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n21 hopper:nat:n22)))
      (rm-block nat-succ-22-23 hopper:rec:nat-succ-22-23
        (quote hopper:nat:proof:succ-22-23)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n22 hopper:nat:n23)))
      (rm-block nat-succ-23-24 hopper:rec:nat-succ-23-24
        (quote hopper:nat:proof:succ-23-24)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n23 hopper:nat:n24)))
      (rm-block nat-succ-24-25 hopper:rec:nat-succ-24-25
        (quote hopper:nat:proof:succ-24-25)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n24 hopper:nat:n25)))
      (rm-block nat-succ-25-26 hopper:rec:nat-succ-25-26
        (quote hopper:nat:proof:succ-25-26)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n25 hopper:nat:n26)))
      (rm-block nat-succ-26-27 hopper:rec:nat-succ-26-27
        (quote hopper:nat:proof:succ-26-27)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n26 hopper:nat:n27)))
      (rm-block nat-succ-27-28 hopper:rec:nat-succ-27-28
        (quote hopper:nat:proof:succ-27-28)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n27 hopper:nat:n28)))
      (rm-block nat-succ-28-29 hopper:rec:nat-succ-28-29
        (quote hopper:nat:proof:succ-28-29)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n28 hopper:nat:n29)))
      (rm-block nat-succ-29-30 hopper:rec:nat-succ-29-30
        (quote hopper:nat:proof:succ-29-30)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n29 hopper:nat:n30)))
      (rm-block nat-succ-30-31 hopper:rec:nat-succ-30-31
        (quote hopper:nat:proof:succ-30-31)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n30 hopper:nat:n31)))
      (rm-block nat-succ-31-32 hopper:rec:nat-succ-31-32
        (quote hopper:nat:proof:succ-31-32)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n31 hopper:nat:n32)))
      (rm-block nat-succ-32-33 hopper:rec:nat-succ-32-33
        (quote hopper:nat:proof:succ-32-33)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n32 hopper:nat:n33)))
      (rm-block nat-succ-33-34 hopper:rec:nat-succ-33-34
        (quote hopper:nat:proof:succ-33-34)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n33 hopper:nat:n34)))
      (rm-block nat-succ-34-35 hopper:rec:nat-succ-34-35
        (quote hopper:nat:proof:succ-34-35)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n34 hopper:nat:n35)))
      (rm-block nat-succ-35-36 hopper:rec:nat-succ-35-36
        (quote hopper:nat:proof:succ-35-36)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n35 hopper:nat:n36)))
      (rm-block nat-pred-clamped-zero hopper:rec:nat-pred-clamped-zero
        (quote hopper:nat:proof:pred-clamped-zero)
        rm-nil
        (quote (hopper:nat:pred-clamped hopper:nat:n0 hopper:nat:n0)))
      (rm-block nat-pred hopper:rec:nat-pred
        (quote (hopper:nat:proof:pred-from-succ
          $later $earlier (unquote $successor-evidence)))
        (rm-cons
          (rm-premise $successor-evidence
            (quote (hopper:nat:succ $earlier $later)))
          rm-nil)
        (quote (hopper:nat:pred $later $earlier)))
      (rm-block nat-pred-clamped-positive
        hopper:rec:nat-pred-clamped-positive
        (quote (hopper:nat:proof:pred-clamped-positive
          $later $earlier (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (hopper:nat:pred $later $earlier)))
          rm-nil)
        (quote (hopper:nat:pred-clamped $later $earlier)))
      (rm-block iterate-zero hopper:rec:iterate-zero
        (quote (rel:iterate:zero $value $counter $step
          $predecessor $zero $source))
        rm-nil
        (quote (rel:iterate $value $counter $step $predecessor
          $zero $source $zero $source)))
      (rm-block iterate-step hopper:rec:iterate-step
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
      (rm-block iterate-with-zero hopper:rec:iterate-with-zero
        (quote (rel:iterate-with:zero $environment $value $counter
          $step $predecessor $zero $parameter $source))
        rm-nil
        (quote (rel:iterate-with $environment $value $counter
          $step $predecessor $zero $parameter
          $source $zero $source)))
      (rm-block iterate-with-step hopper:rec:iterate-with-step
        (quote (rel:iterate-with:step $environment $value $counter
          $step $predecessor $zero $parameter $source $next
          $later $earlier $target
          (unquote $predecessor-evidence)
          (unquote $step-evidence)
          (unquote $recursive-evidence)))
        (rm-cons
          (rm-premise $predecessor-evidence
            (quote ($predecessor $later $earlier)))
          (rm-cons
            (rm-premise $step-evidence
              (quote ($step $parameter $source $next)))
            (rm-cons
              (rm-premise $recursive-evidence
                (quote (rel:iterate-with $environment $value $counter
                  $step $predecessor $zero $parameter
                  $next $earlier $target)))
              rm-nil)))
        (quote (rel:iterate-with $environment $value $counter
          $step $predecessor $zero $parameter
          $source $later $target)))
      (rm-block list-repeat-zero hopper:rec:list-repeat-zero
        (quote (rel:list:repeat:zero $element $counter
          $predecessor $zero $item $before))
        rm-nil
        (quote (rel:list:repeat $element $counter
          $predecessor $zero $item $before $zero $before)))
      (rm-block list-repeat-step hopper:rec:list-repeat-step
        (quote (rel:list:repeat:step $element $counter
          $predecessor $zero $item $before $next
          $later $earlier $after
          (unquote $predecessor-evidence)
          (unquote $append-evidence)
          (unquote $recursive-evidence)))
        (rm-cons
          (rm-premise $predecessor-evidence
            (quote ($predecessor $later $earlier)))
          (rm-cons
            (rm-premise $append-evidence
              (quote (rel:list:snoc $element $before $item $next)))
            (rm-cons
              (rm-premise $recursive-evidence
                (quote (rel:list:repeat $element $counter
                  $predecessor $zero $item $next $earlier $after)))
              rm-nil)))
        (quote (rel:list:repeat $element $counter
          $predecessor $zero $item $before $later $after)))
      (rm-block unfold-zero hopper:rec:unfold-zero
        (quote (rel:unfold:zero $state $counter $output
          $advance $emit $zero $current))
        rm-nil
        (quote (rel:unfold $state $counter $output
          $advance $emit $zero $current $zero
          (list:nil $output))))
      (rm-block unfold-step hopper:rec:unfold-step
        (quote (rel:unfold:step $state $counter $output
          $advance $emit $zero $current $head $next
          $later $earlier $tail
          (unquote $advance-evidence)
          (unquote $emit-evidence)
          (unquote $recursive-evidence)))
        (rm-cons
          (rm-premise $advance-evidence
            (quote ($advance $later $earlier)))
          (rm-cons
            (rm-premise $emit-evidence
              (quote ($emit $current $head $next)))
            (rm-cons
              (rm-premise $recursive-evidence
                (quote (rel:unfold $state $counter $output
                  $advance $emit $zero $next $earlier $tail)))
              rm-nil)))
        (quote (rel:unfold $state $counter $output
          $advance $emit $zero $current $later
          (list:cons $output $head $tail))))
      (rm-block map-rel-nil hopper:rec:map-rel-nil
        (quote (map-rel:nil $source $target $relation))
        rm-nil
        (quote (map-rel $source $target $relation
          (list:nil $source) (list:nil $target))))
      (rm-block map-rel-cons hopper:rec:map-rel-cons
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
      (rm-block fold-nil hopper:rec:fold-nil
        (quote (rel:fold:nil $element $accumulator $step $before))
        rm-nil
        (quote (rel:fold $element $accumulator $step $before
          (list:nil $element) $before)))
      (rm-block fold-cons hopper:rec:fold-cons
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
      (rm-block length-step hopper:rec:length-step
        (quote (hopper:length:step-proof $before $head $after
          (unquote $successor-evidence)))
        (rm-cons
          (rm-premise $successor-evidence
            (quote (hopper:nat:succ $before $after)))
          rm-nil)
        (quote (hopper:length:step $before $head $after)))
      (rm-block length hopper:rec:length
        (quote (hopper:length:proof $values $length
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:fold hopper:atom hopper:nat
              hopper:length:step hopper:nat:n0 $values $length)))
          rm-nil)
        (quote (hopper:length:f $values $length)))
      (rm-block repeat-n hopper:rec:repeat-n
        (quote (hopper:repeat-n:proof $element $source $count
          $target (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:list:repeat (list $element) hopper:nat
              hopper:nat:pred hopper:nat:n0 $source
              (list:nil (list $element)) $count $target)))
          rm-nil)
        (quote (hopper:repeat-n:f $element $source $count $target)))
      (rm-block successor-emit hopper:rec:successor-emit
        (quote (hopper:all-seq-n:successor-emit-proof
          $current $next (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (hopper:nat:succ $current $next)))
          rm-nil)
        (quote (hopper:all-seq-n:successor-emit
          $current $next $next)))
      (rm-block all-seq-prefix hopper:rec:all-seq-prefix
        (quote (hopper:all-seq-n:prefix-proof $count $values
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:unfold hopper:nat hopper:nat hopper:nat
              hopper:nat:pred hopper:all-seq-n:successor-emit
              hopper:nat:n0 hopper:nat:n0 $count $values)))
          rm-nil)
        (quote (hopper:all-seq-n:prefix $count $values)))
      (rm-block all-seq-n hopper:rec:all-seq-n
        (quote (hopper:all-seq-n:proof $count $indices $values
          (unquote $scan-evidence) (unquote $map-evidence)))
        (rm-cons
          (rm-premise $scan-evidence
            (quote (rel:unfold hopper:nat hopper:nat hopper:nat
              hopper:nat:pred hopper:all-seq-n:successor-emit
              hopper:nat:n0 hopper:nat:n0 $count $indices)))
          (rm-cons
            (rm-premise $map-evidence
              (quote (map-rel hopper:nat (list hopper:nat)
                hopper:all-seq-n:prefix $indices $values)))
            rm-nil))
        (quote (hopper:all-seq-n:f $count $values)))
      (rm-block first-half-advance hopper:rec:first-half-advance
        (quote (hopper:first-half:advance-proof
          $later $middle $earlier
          (unquote $first) (unquote $second)))
        (rm-cons
          (rm-premise $first
            (quote (hopper:nat:pred-clamped $later $middle)))
          (rm-cons
            (rm-premise $second
              (quote (hopper:nat:pred-clamped $middle $earlier)))
            rm-nil))
        (quote (hopper:first-half:advance $later $earlier)))
      (rm-block first-half-emit hopper:rec:first-half-emit
        (quote (hopper:first-half:emit-proof
          $current $head $next
          (unquote $head-evidence) (unquote $tail-evidence)))
        (rm-cons
          (rm-premise $head-evidence
            (quote (rel:list:head hopper:atom $current $head)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (rel:list:tail hopper:atom $current $next)))
            rm-nil))
        (quote (hopper:first-half:emit $current $head $next)))
      (rm-block first-half hopper:rec:first-half
        (quote (hopper:first-half:proof $source $count $target
          (unquote $length-evidence) (unquote $unfold-evidence)))
        (rm-cons
          (rm-premise $length-evidence
            (quote (hopper:length:f $source $count)))
          (rm-cons
            (rm-premise $unfold-evidence
              (quote (rel:unfold (list hopper:atom) hopper:nat
                hopper:atom hopper:first-half:advance
                hopper:first-half:emit hopper:nat:n0
                $source $count $target)))
            rm-nil))
        (quote (hopper:first-half:f $source $target)))
      (rm-block nat-add-from-suc hopper:rec:nat-add-from-suc
        (quote (hopper:nat:add-from-suc-proof
          $amount $source $target (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:iterate hopper:nat hopper:nat
              hopper:nat:succ hopper:nat:pred hopper:nat:n0
              $source $amount $target)))
          rm-nil)
        (quote (hopper:nat:add-from-suc
          $amount $source $target)))
      (rm-block mul-from-suc hopper:rec:mul-from-suc
        (quote (hopper:mul-from-suc:proof
          $left $right $target (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:iterate-with hopper:nat hopper:nat hopper:nat
              hopper:nat:add-from-suc hopper:nat:pred hopper:nat:n0
              $right hopper:nat:n0 $left $target)))
          rm-nil)
        (quote (hopper:mul-from-suc:f $left $right $target)))
    )))
