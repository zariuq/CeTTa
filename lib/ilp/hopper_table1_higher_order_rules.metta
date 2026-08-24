; Proof-producing realization of Hopper's two HO programs.
; `map-rel` remains generic; task relations are ordinary typed values.

(= (hopper:table1:higher-order:package)
  (compile:rule-package hopper-table1-higher-order-v1
    (rm-package
      (rm-block list-tail hopper:ho:list-tail
        (quote (rel:list:tail-proof $element $head $tail))
        rm-nil
        (quote (rel:list:tail $element
          (list:cons $element $head $tail) $tail)))
      (rm-block fold-nil hopper:ho:fold-nil
        (quote (rel:fold:nil $element $accumulator $step $before))
        rm-nil
        (quote (rel:fold $element $accumulator $step $before
          (list:nil $element) $before)))
      (rm-block fold-cons hopper:ho:fold-cons
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
      (rm-block reverse-step hopper:ho:reverse-step
        (quote (hopper:reverse:step-proof $before $head
          (rel:list:head-proof hopper:atom $head $before)
          (rel:list:tail-proof hopper:atom $head $before)))
        rm-nil
        (quote (hopper:reverse:step $before $head
          (list:cons hopper:atom $head $before))))
      (rm-block reverse hopper:ho:reverse
        (quote (hopper:reverse:proof $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (rel:fold hopper:atom (list hopper:atom)
              hopper:reverse:step (list:nil hopper:atom)
              $source $target)))
          rm-nil)
        (quote (hopper:reverse:f $source $target)))
      (rm-block map-rel-nil hopper:ho:map-rel-nil
        (quote (map-rel:nil $source $target $relation))
        rm-nil
        (quote (map-rel $source $target $relation
          (list:nil $source) (list:nil $target))))
      (rm-block map-rel-cons hopper:ho:map-rel-cons
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
      (rm-block drop-last-element hopper:ho:drop-last-element
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
      (rm-block target-drop-last hopper:ho:target-drop-last
        (quote (hopper:drop-last:proof $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (map-rel (list hopper:atom) (list hopper:atom)
              hopper:drop-last:element $source $target)))
          rm-nil)
        (quote (hopper:drop-last:f $source $target)))
      (rm-block char-to-num-a hopper:ho:char-to-num-a
        (quote hopper:cipher:proof:char-to-num-a)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:a hopper:cipher-num:n0)))
      (rm-block char-to-num-b hopper:ho:char-to-num-b
        (quote hopper:cipher:proof:char-to-num-b)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:b hopper:cipher-num:n1)))
      (rm-block char-to-num-c hopper:ho:char-to-num-c
        (quote hopper:cipher:proof:char-to-num-c)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:c hopper:cipher-num:n2)))
      (rm-block char-to-num-d hopper:ho:char-to-num-d
        (quote hopper:cipher:proof:char-to-num-d)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:d hopper:cipher-num:n3)))
      (rm-block char-to-num-e hopper:ho:char-to-num-e
        (quote hopper:cipher:proof:char-to-num-e)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:e hopper:cipher-num:n4)))
      (rm-block char-to-num-f hopper:ho:char-to-num-f
        (quote hopper:cipher:proof:char-to-num-f)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:f hopper:cipher-num:n5)))
      (rm-block char-to-num-g hopper:ho:char-to-num-g
        (quote hopper:cipher:proof:char-to-num-g)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:g hopper:cipher-num:n6)))
      (rm-block char-to-num-h hopper:ho:char-to-num-h
        (quote hopper:cipher:proof:char-to-num-h)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:h hopper:cipher-num:n7)))
      (rm-block char-to-num-i hopper:ho:char-to-num-i
        (quote hopper:cipher:proof:char-to-num-i)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:i hopper:cipher-num:n8)))
      (rm-block char-to-num-j hopper:ho:char-to-num-j
        (quote hopper:cipher:proof:char-to-num-j)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:j hopper:cipher-num:n9)))
      (rm-block char-to-num-k hopper:ho:char-to-num-k
        (quote hopper:cipher:proof:char-to-num-k)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:k hopper:cipher-num:n10)))
      (rm-block char-to-num-l hopper:ho:char-to-num-l
        (quote hopper:cipher:proof:char-to-num-l)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:l hopper:cipher-num:n11)))
      (rm-block char-to-num-m hopper:ho:char-to-num-m
        (quote hopper:cipher:proof:char-to-num-m)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:m hopper:cipher-num:n12)))
      (rm-block char-to-num-n hopper:ho:char-to-num-n
        (quote hopper:cipher:proof:char-to-num-n)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:n hopper:cipher-num:n13)))
      (rm-block char-to-num-o hopper:ho:char-to-num-o
        (quote hopper:cipher:proof:char-to-num-o)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:o hopper:cipher-num:n14)))
      (rm-block char-to-num-p hopper:ho:char-to-num-p
        (quote hopper:cipher:proof:char-to-num-p)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:p hopper:cipher-num:n15)))
      (rm-block char-to-num-q hopper:ho:char-to-num-q
        (quote hopper:cipher:proof:char-to-num-q)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:q hopper:cipher-num:n16)))
      (rm-block char-to-num-r hopper:ho:char-to-num-r
        (quote hopper:cipher:proof:char-to-num-r)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:r hopper:cipher-num:n17)))
      (rm-block char-to-num-s hopper:ho:char-to-num-s
        (quote hopper:cipher:proof:char-to-num-s)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:s hopper:cipher-num:n18)))
      (rm-block char-to-num-t hopper:ho:char-to-num-t
        (quote hopper:cipher:proof:char-to-num-t)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:t hopper:cipher-num:n19)))
      (rm-block char-to-num-u hopper:ho:char-to-num-u
        (quote hopper:cipher:proof:char-to-num-u)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:u hopper:cipher-num:n20)))
      (rm-block char-to-num-v hopper:ho:char-to-num-v
        (quote hopper:cipher:proof:char-to-num-v)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:v hopper:cipher-num:n21)))
      (rm-block char-to-num-w hopper:ho:char-to-num-w
        (quote hopper:cipher:proof:char-to-num-w)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:w hopper:cipher-num:n22)))
      (rm-block char-to-num-x hopper:ho:char-to-num-x
        (quote hopper:cipher:proof:char-to-num-x)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:x hopper:cipher-num:n23)))
      (rm-block char-to-num-y hopper:ho:char-to-num-y
        (quote hopper:cipher:proof:char-to-num-y)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:y hopper:cipher-num:n24)))
      (rm-block char-to-num-z hopper:ho:char-to-num-z
        (quote hopper:cipher:proof:char-to-num-z)
        rm-nil
        (quote (hopper:cipher:char-to-num hopper:char:z hopper:cipher-num:n25)))
      (rm-block num-to-char-a hopper:ho:num-to-char-a
        (quote hopper:cipher:proof:num-to-char-a)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n0 hopper:char:a)))
      (rm-block num-to-char-b hopper:ho:num-to-char-b
        (quote hopper:cipher:proof:num-to-char-b)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n1 hopper:char:b)))
      (rm-block num-to-char-c hopper:ho:num-to-char-c
        (quote hopper:cipher:proof:num-to-char-c)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n2 hopper:char:c)))
      (rm-block num-to-char-d hopper:ho:num-to-char-d
        (quote hopper:cipher:proof:num-to-char-d)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n3 hopper:char:d)))
      (rm-block num-to-char-e hopper:ho:num-to-char-e
        (quote hopper:cipher:proof:num-to-char-e)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n4 hopper:char:e)))
      (rm-block num-to-char-f hopper:ho:num-to-char-f
        (quote hopper:cipher:proof:num-to-char-f)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n5 hopper:char:f)))
      (rm-block num-to-char-g hopper:ho:num-to-char-g
        (quote hopper:cipher:proof:num-to-char-g)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n6 hopper:char:g)))
      (rm-block num-to-char-h hopper:ho:num-to-char-h
        (quote hopper:cipher:proof:num-to-char-h)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n7 hopper:char:h)))
      (rm-block num-to-char-i hopper:ho:num-to-char-i
        (quote hopper:cipher:proof:num-to-char-i)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n8 hopper:char:i)))
      (rm-block num-to-char-j hopper:ho:num-to-char-j
        (quote hopper:cipher:proof:num-to-char-j)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n9 hopper:char:j)))
      (rm-block num-to-char-k hopper:ho:num-to-char-k
        (quote hopper:cipher:proof:num-to-char-k)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n10 hopper:char:k)))
      (rm-block num-to-char-l hopper:ho:num-to-char-l
        (quote hopper:cipher:proof:num-to-char-l)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n11 hopper:char:l)))
      (rm-block num-to-char-m hopper:ho:num-to-char-m
        (quote hopper:cipher:proof:num-to-char-m)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n12 hopper:char:m)))
      (rm-block num-to-char-n hopper:ho:num-to-char-n
        (quote hopper:cipher:proof:num-to-char-n)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n13 hopper:char:n)))
      (rm-block num-to-char-o hopper:ho:num-to-char-o
        (quote hopper:cipher:proof:num-to-char-o)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n14 hopper:char:o)))
      (rm-block num-to-char-p hopper:ho:num-to-char-p
        (quote hopper:cipher:proof:num-to-char-p)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n15 hopper:char:p)))
      (rm-block num-to-char-q hopper:ho:num-to-char-q
        (quote hopper:cipher:proof:num-to-char-q)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n16 hopper:char:q)))
      (rm-block num-to-char-r hopper:ho:num-to-char-r
        (quote hopper:cipher:proof:num-to-char-r)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n17 hopper:char:r)))
      (rm-block num-to-char-s hopper:ho:num-to-char-s
        (quote hopper:cipher:proof:num-to-char-s)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n18 hopper:char:s)))
      (rm-block num-to-char-t hopper:ho:num-to-char-t
        (quote hopper:cipher:proof:num-to-char-t)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n19 hopper:char:t)))
      (rm-block num-to-char-u hopper:ho:num-to-char-u
        (quote hopper:cipher:proof:num-to-char-u)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n20 hopper:char:u)))
      (rm-block num-to-char-v hopper:ho:num-to-char-v
        (quote hopper:cipher:proof:num-to-char-v)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n21 hopper:char:v)))
      (rm-block num-to-char-w hopper:ho:num-to-char-w
        (quote hopper:cipher:proof:num-to-char-w)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n22 hopper:char:w)))
      (rm-block num-to-char-x hopper:ho:num-to-char-x
        (quote hopper:cipher:proof:num-to-char-x)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n23 hopper:char:x)))
      (rm-block num-to-char-y hopper:ho:num-to-char-y
        (quote hopper:cipher:proof:num-to-char-y)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n24 hopper:char:y)))
      (rm-block num-to-char-z hopper:ho:num-to-char-z
        (quote hopper:cipher:proof:num-to-char-z)
        rm-nil
        (quote (hopper:cipher:num-to-char hopper:cipher-num:n25 hopper:char:z)))
      (rm-block pred-0-25 hopper:ho:pred-0-25
        (quote hopper:cipher:proof:pred-0-25)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n0 hopper:cipher-num:n25)))
      (rm-block pred-1-0 hopper:ho:pred-1-0
        (quote hopper:cipher:proof:pred-1-0)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n1 hopper:cipher-num:n0)))
      (rm-block pred-2-1 hopper:ho:pred-2-1
        (quote hopper:cipher:proof:pred-2-1)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n2 hopper:cipher-num:n1)))
      (rm-block pred-3-2 hopper:ho:pred-3-2
        (quote hopper:cipher:proof:pred-3-2)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n3 hopper:cipher-num:n2)))
      (rm-block pred-4-3 hopper:ho:pred-4-3
        (quote hopper:cipher:proof:pred-4-3)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n4 hopper:cipher-num:n3)))
      (rm-block pred-5-4 hopper:ho:pred-5-4
        (quote hopper:cipher:proof:pred-5-4)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n5 hopper:cipher-num:n4)))
      (rm-block pred-6-5 hopper:ho:pred-6-5
        (quote hopper:cipher:proof:pred-6-5)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n6 hopper:cipher-num:n5)))
      (rm-block pred-7-6 hopper:ho:pred-7-6
        (quote hopper:cipher:proof:pred-7-6)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n7 hopper:cipher-num:n6)))
      (rm-block pred-8-7 hopper:ho:pred-8-7
        (quote hopper:cipher:proof:pred-8-7)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n8 hopper:cipher-num:n7)))
      (rm-block pred-9-8 hopper:ho:pred-9-8
        (quote hopper:cipher:proof:pred-9-8)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n9 hopper:cipher-num:n8)))
      (rm-block pred-10-9 hopper:ho:pred-10-9
        (quote hopper:cipher:proof:pred-10-9)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n10 hopper:cipher-num:n9)))
      (rm-block pred-11-10 hopper:ho:pred-11-10
        (quote hopper:cipher:proof:pred-11-10)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n11 hopper:cipher-num:n10)))
      (rm-block pred-12-11 hopper:ho:pred-12-11
        (quote hopper:cipher:proof:pred-12-11)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n12 hopper:cipher-num:n11)))
      (rm-block pred-13-12 hopper:ho:pred-13-12
        (quote hopper:cipher:proof:pred-13-12)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n13 hopper:cipher-num:n12)))
      (rm-block pred-14-13 hopper:ho:pred-14-13
        (quote hopper:cipher:proof:pred-14-13)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n14 hopper:cipher-num:n13)))
      (rm-block pred-15-14 hopper:ho:pred-15-14
        (quote hopper:cipher:proof:pred-15-14)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n15 hopper:cipher-num:n14)))
      (rm-block pred-16-15 hopper:ho:pred-16-15
        (quote hopper:cipher:proof:pred-16-15)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n16 hopper:cipher-num:n15)))
      (rm-block pred-17-16 hopper:ho:pred-17-16
        (quote hopper:cipher:proof:pred-17-16)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n17 hopper:cipher-num:n16)))
      (rm-block pred-18-17 hopper:ho:pred-18-17
        (quote hopper:cipher:proof:pred-18-17)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n18 hopper:cipher-num:n17)))
      (rm-block pred-19-18 hopper:ho:pred-19-18
        (quote hopper:cipher:proof:pred-19-18)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n19 hopper:cipher-num:n18)))
      (rm-block pred-20-19 hopper:ho:pred-20-19
        (quote hopper:cipher:proof:pred-20-19)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n20 hopper:cipher-num:n19)))
      (rm-block pred-21-20 hopper:ho:pred-21-20
        (quote hopper:cipher:proof:pred-21-20)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n21 hopper:cipher-num:n20)))
      (rm-block pred-22-21 hopper:ho:pred-22-21
        (quote hopper:cipher:proof:pred-22-21)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n22 hopper:cipher-num:n21)))
      (rm-block pred-23-22 hopper:ho:pred-23-22
        (quote hopper:cipher:proof:pred-23-22)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n23 hopper:cipher-num:n22)))
      (rm-block pred-24-23 hopper:ho:pred-24-23
        (quote hopper:cipher:proof:pred-24-23)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n24 hopper:cipher-num:n23)))
      (rm-block pred-25-24 hopper:ho:pred-25-24
        (quote hopper:cipher:proof:pred-25-24)
        rm-nil
        (quote (hopper:cipher:pred hopper:cipher-num:n25 hopper:cipher-num:n24)))
      (rm-block cipher-element hopper:ho:cipher-element
        (quote (hopper:cipher:element-proof
          $source $source-number $middle-number $target-number $target
          (unquote $encode) (unquote $first-predecessor)
          (unquote $second-predecessor) (unquote $decode)))
        (rm-cons
          (rm-premise $encode
            (quote (hopper:cipher:char-to-num $source $source-number)))
          (rm-cons
            (rm-premise $first-predecessor
              (quote (hopper:cipher:pred
                $source-number $middle-number)))
            (rm-cons
              (rm-premise $second-predecessor
                (quote (hopper:cipher:pred
                  $middle-number $target-number)))
              (rm-cons
                (rm-premise $decode
                  (quote (hopper:cipher:num-to-char
                    $target-number $target)))
                rm-nil))))
        (quote (hopper:cipher:element $source $target)))
      (rm-block target-encryption hopper:ho:target-encryption
        (quote (hopper:encryption:proof $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (map-rel hopper:char hopper:char
              hopper:cipher:element $source $target)))
          rm-nil)
        (quote (hopper:encryption:f $source $target)))
      (rm-block branch-left hopper:ho:branch-left
        (quote hopper:cipher:branch-left)
        rm-nil
        (quote (hopper:cipher:branch hopper:char:a hopper:char:y)))
      (rm-block branch-right hopper:ho:branch-right
        (quote hopper:cipher:branch-right)
        rm-nil
        (quote (hopper:cipher:branch hopper:char:a hopper:char:y)))
      (rm-block target-branch hopper:ho:target-branch
        (quote (hopper:cipher:branch-proof $source $target
          (unquote $evidence)))
        (rm-cons
          (rm-premise $evidence
            (quote (map-rel hopper:char hopper:char
              hopper:cipher:branch $source $target)))
          rm-nil)
        (quote (hopper:cipher:branch-f $source $target)))
    )))
