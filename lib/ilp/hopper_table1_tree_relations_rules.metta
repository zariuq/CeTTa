; Proof-producing execution of the three authored rose-tree programs.
; Tree data remains at the explicit checked boundary; recursive
; evidence and every child occurrence remain first-class.

(= (hopper:table1:tree-relations:package)
  (compile:rule-package hopper-table1-tree-relations-v1
    (rm-package
      (rm-block list-empty hopper:tree:list-empty
        (quote (rel:list:empty-proof $element))
        rm-nil
        (quote (rel:list:empty $element (list:nil $element))))
      (rm-block list-head hopper:tree:list-head
        (quote (rel:list:head-proof $element $head $tail))
        rm-nil
        (quote (rel:list:head $element
          (list:cons $element $head $tail) $head)))
      (rm-block list-tail hopper:tree:list-tail
        (quote (rel:list:tail-proof $element $head $tail))
        rm-nil
        (quote (rel:list:tail $element
          (list:cons $element $head $tail) $tail)))
      (rm-block tree-view hopper:tree:view
        (quote (hopper:tree:view:node $root $children))
        rm-nil
        (quote (hopper:tree:view
          (hopper:tree:node $root $children) $root $children)))
      (rm-block nat-succ-0-1 hopper:tree:nat-succ-0-1
        (quote hopper:nat:proof:succ-0-1)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n0 hopper:nat:n1)))
      (rm-block nat-succ-1-2 hopper:tree:nat-succ-1-2
        (quote hopper:nat:proof:succ-1-2)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n1 hopper:nat:n2)))
      (rm-block nat-succ-2-3 hopper:tree:nat-succ-2-3
        (quote hopper:nat:proof:succ-2-3)
        rm-nil
        (quote (hopper:nat:succ hopper:nat:n2 hopper:nat:n3)))
      (rm-block nat-max-0-0 hopper:tree:nat-max-0-0
        (quote hopper:nat:proof:max-0-0)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n0 hopper:nat:n0 hopper:nat:n0)))
      (rm-block nat-max-0-1 hopper:tree:nat-max-0-1
        (quote hopper:nat:proof:max-0-1)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n0 hopper:nat:n1 hopper:nat:n1)))
      (rm-block nat-max-0-2 hopper:tree:nat-max-0-2
        (quote hopper:nat:proof:max-0-2)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n0 hopper:nat:n2 hopper:nat:n2)))
      (rm-block nat-max-0-3 hopper:tree:nat-max-0-3
        (quote hopper:nat:proof:max-0-3)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n0 hopper:nat:n3 hopper:nat:n3)))
      (rm-block nat-max-1-0 hopper:tree:nat-max-1-0
        (quote hopper:nat:proof:max-1-0)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n1 hopper:nat:n0 hopper:nat:n1)))
      (rm-block nat-max-1-1 hopper:tree:nat-max-1-1
        (quote hopper:nat:proof:max-1-1)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n1 hopper:nat:n1 hopper:nat:n1)))
      (rm-block nat-max-1-2 hopper:tree:nat-max-1-2
        (quote hopper:nat:proof:max-1-2)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n1 hopper:nat:n2 hopper:nat:n2)))
      (rm-block nat-max-1-3 hopper:tree:nat-max-1-3
        (quote hopper:nat:proof:max-1-3)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n1 hopper:nat:n3 hopper:nat:n3)))
      (rm-block nat-max-2-0 hopper:tree:nat-max-2-0
        (quote hopper:nat:proof:max-2-0)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n2 hopper:nat:n0 hopper:nat:n2)))
      (rm-block nat-max-2-1 hopper:tree:nat-max-2-1
        (quote hopper:nat:proof:max-2-1)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n2 hopper:nat:n1 hopper:nat:n2)))
      (rm-block nat-max-2-2 hopper:tree:nat-max-2-2
        (quote hopper:nat:proof:max-2-2)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n2 hopper:nat:n2 hopper:nat:n2)))
      (rm-block nat-max-2-3 hopper:tree:nat-max-2-3
        (quote hopper:nat:proof:max-2-3)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n2 hopper:nat:n3 hopper:nat:n3)))
      (rm-block nat-max-3-0 hopper:tree:nat-max-3-0
        (quote hopper:nat:proof:max-3-0)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n3 hopper:nat:n0 hopper:nat:n3)))
      (rm-block nat-max-3-1 hopper:tree:nat-max-3-1
        (quote hopper:nat:proof:max-3-1)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n3 hopper:nat:n1 hopper:nat:n3)))
      (rm-block nat-max-3-2 hopper:tree:nat-max-3-2
        (quote hopper:nat:proof:max-3-2)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n3 hopper:nat:n2 hopper:nat:n3)))
      (rm-block nat-max-3-3 hopper:tree:nat-max-3-3
        (quote hopper:nat:proof:max-3-3)
        rm-nil
        (quote (hopper:nat:max hopper:nat:n3 hopper:nat:n3 hopper:nat:n3)))
      (rm-block fold-nil hopper:tree:fold-nil
        (quote (rel:fold:nil $element $accumulator $step $before))
        rm-nil
        (quote (rel:fold $element $accumulator $step $before
          (list:nil $element) $before)))
      (rm-block fold-cons hopper:tree:fold-cons
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
      (rm-block any-here hopper:tree:any-here
        (quote (rel:any:here $element $target $predicate
          $values $answer (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence (quote ($predicate $values $answer)))
          rm-nil)
        (quote (rel:any $element $target $predicate
          $values $answer)))
      (rm-block any-there hopper:tree:any-there
        (quote (rel:any:there $element $target $predicate
          $head $tail $answer (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:any $element $target $predicate
              $tail $answer)))
          rm-nil)
        (quote (rel:any $element $target $predicate
          (list:cons $element $head $tail) $answer)))
      (rm-block depth-step hopper:tree:depth-step
        (quote (hopper:depth:step-proof
          $before $child $child-depth $after
          (unquote $depth-evidence) (unquote $max-evidence)))
        (rm-cons
          (rm-premise $depth-evidence
            (quote (hopper:depth:f $child $child-depth)))
          (rm-cons
            (rm-premise $max-evidence
              (quote (hopper:nat:max $child-depth $before $after)))
            rm-nil))
        (quote (hopper:depth:step $before $child $after)))
      (rm-block depth hopper:tree:depth
        (quote (hopper:depth:proof
          $tree $root $children $maximum $depth
          (unquote $view-evidence) (unquote $fold-evidence)
          (unquote $successor-evidence)))
        (rm-cons
          (rm-premise $view-evidence
            (quote (hopper:tree:view $tree $root $children)))
          (rm-cons
            (rm-premise $fold-evidence
              (quote (rel:fold hopper:tree hopper:nat
                hopper:depth:step hopper:nat:n0
                $children $maximum)))
            (rm-cons
              (rm-premise $successor-evidence
                (quote (hopper:nat:succ $maximum $depth)))
              rm-nil)))
        (quote (hopper:depth:f $tree $depth)))
      (rm-block branch-any-child hopper:tree:branch-any-child
        (quote (hopper:is-branch:any-child-proof
          $remaining $child $path $path-tail
          (unquote $child-evidence) (unquote $tail-evidence)
          (unquote $branch-evidence)))
        (rm-cons
          (rm-premise $child-evidence
            (quote (rel:list:head hopper:tree
              $remaining $child)))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (rel:list:tail hopper:nat
                $path $path-tail)))
            (rm-cons
              (rm-premise $branch-evidence
                (quote (hopper:is-branch:f $child $path-tail)))
              rm-nil)))
        (quote (hopper:is-branch:any-child $remaining $path)))
      (rm-block branch-leaf hopper:tree:branch-leaf
        (quote (hopper:is-branch:proof:leaf
          $root $path $path-tail
          (unquote $view-evidence) (unquote $tail-evidence)
          (unquote $empty-evidence) (unquote $head-evidence)))
        (rm-cons
          (rm-premise $view-evidence
            (quote (hopper:tree:view
              (hopper:tree:node $root (list:nil hopper:tree))
              $root (list:nil hopper:tree))))
          (rm-cons
            (rm-premise $tail-evidence
              (quote (rel:list:tail hopper:nat
                $path $path-tail)))
            (rm-cons
              (rm-premise $empty-evidence
                (quote (rel:list:empty hopper:nat $path-tail)))
              (rm-cons
                (rm-premise $head-evidence
                  (quote (rel:list:head hopper:nat $path $root)))
                rm-nil))))
        (quote (hopper:is-branch:f
          (hopper:tree:node $root (list:nil hopper:tree)) $path)))
      (rm-block branch-node hopper:tree:branch-node
        (quote (hopper:is-branch:proof:node
          $root $first $rest $path
          (unquote $view-evidence) (unquote $head-evidence)
          (unquote $any-evidence)))
        (rm-cons
          (rm-premise $view-evidence
            (quote (hopper:tree:view
              (hopper:tree:node $root
                (list:cons hopper:tree $first $rest))
              $root (list:cons hopper:tree $first $rest))))
          (rm-cons
            (rm-premise $head-evidence
              (quote (rel:list:head hopper:nat $path $root)))
            (rm-cons
              (rm-premise $any-evidence
                (quote (rel:any hopper:tree (list hopper:nat)
                  hopper:is-branch:any-child
                  (list:cons hopper:tree $first $rest) $path)))
              rm-nil)))
        (quote (hopper:is-branch:f
          (hopper:tree:node $root
            (list:cons hopper:tree $first $rest)) $path)))
      (rm-block subtree-equal hopper:tree:subtree-equal
        (quote (hopper:is-subtree:proof:equal $tree))
        rm-nil
        (quote (hopper:is-subtree:f $tree $tree)))
      (rm-block subtree-any-child hopper:tree:subtree-any-child
        (quote (hopper:is-subtree:any-child-proof
          $remaining $child $target
          (unquote $child-evidence) (unquote $subtree-evidence)))
        (rm-cons
          (rm-premise $child-evidence
            (quote (rel:list:head hopper:tree
              $remaining $child)))
          (rm-cons
            (rm-premise $subtree-evidence
              (quote (hopper:is-subtree:f $child $target)))
            rm-nil))
        (quote (hopper:is-subtree:any-child
          $remaining $target)))
      (rm-block subtree-child hopper:tree:subtree-child
        (quote (hopper:is-subtree:proof:child
          $root $children $target
          (unquote $view-evidence) (unquote $any-evidence)))
        (rm-cons
          (rm-premise $view-evidence
            (quote (hopper:tree:view
              (hopper:tree:node $root $children)
              $root $children)))
          (rm-cons
            (rm-premise $any-evidence
              (quote (rel:any hopper:tree hopper:tree
                hopper:is-subtree:any-child
                $children $target)))
            rm-nil))
        (quote (hopper:is-subtree:f
          (hopper:tree:node $root $children) $target)))
    )))
