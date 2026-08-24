; Typed source-facing rose trees and proof-relevant task relations.
; `hopper:tree:node` is checked at the raw boundary.  It does not
; claim generic native-family or eliminator authority.

(: hopper:tree (u 0))
(: hopper:tree:node
  (-> (label : hopper:nat) (children : (list hopper:tree))
      hopper:tree))
(: hopper:tree:view
  (-> (tree : hopper:tree) (label : hopper:nat)
      (children : (list hopper:tree)) (u 0)))
(: hopper:tree:view:node
  (-> (label : hopper:nat) (children : (list hopper:tree))
      (hopper:tree:view
        (hopper:tree:node label children) label children)))

(: hopper:nat:max
  (-> (left : hopper:nat) (right : hopper:nat)
      (result : hopper:nat) (u 0)))
(: hopper:nat:proof:max-0-0 (hopper:nat:max hopper:nat:n0 hopper:nat:n0 hopper:nat:n0))
(: hopper:nat:proof:max-0-1 (hopper:nat:max hopper:nat:n0 hopper:nat:n1 hopper:nat:n1))
(: hopper:nat:proof:max-0-2 (hopper:nat:max hopper:nat:n0 hopper:nat:n2 hopper:nat:n2))
(: hopper:nat:proof:max-0-3 (hopper:nat:max hopper:nat:n0 hopper:nat:n3 hopper:nat:n3))
(: hopper:nat:proof:max-1-0 (hopper:nat:max hopper:nat:n1 hopper:nat:n0 hopper:nat:n1))
(: hopper:nat:proof:max-1-1 (hopper:nat:max hopper:nat:n1 hopper:nat:n1 hopper:nat:n1))
(: hopper:nat:proof:max-1-2 (hopper:nat:max hopper:nat:n1 hopper:nat:n2 hopper:nat:n2))
(: hopper:nat:proof:max-1-3 (hopper:nat:max hopper:nat:n1 hopper:nat:n3 hopper:nat:n3))
(: hopper:nat:proof:max-2-0 (hopper:nat:max hopper:nat:n2 hopper:nat:n0 hopper:nat:n2))
(: hopper:nat:proof:max-2-1 (hopper:nat:max hopper:nat:n2 hopper:nat:n1 hopper:nat:n2))
(: hopper:nat:proof:max-2-2 (hopper:nat:max hopper:nat:n2 hopper:nat:n2 hopper:nat:n2))
(: hopper:nat:proof:max-2-3 (hopper:nat:max hopper:nat:n2 hopper:nat:n3 hopper:nat:n3))
(: hopper:nat:proof:max-3-0 (hopper:nat:max hopper:nat:n3 hopper:nat:n0 hopper:nat:n3))
(: hopper:nat:proof:max-3-1 (hopper:nat:max hopper:nat:n3 hopper:nat:n1 hopper:nat:n3))
(: hopper:nat:proof:max-3-2 (hopper:nat:max hopper:nat:n3 hopper:nat:n2 hopper:nat:n3))
(: hopper:nat:proof:max-3-3 (hopper:nat:max hopper:nat:n3 hopper:nat:n3 hopper:nat:n3))

(: hopper:depth:step
  (-> (before : hopper:nat) (child : hopper:tree)
      (after : hopper:nat) (u 0)))
(: hopper:depth:step-proof
  (-> (before : hopper:nat) (child : hopper:tree)
      (child-depth : hopper:nat) (after : hopper:nat)
      (depth-evidence : (hopper:depth:f child child-depth))
      (max-evidence :
        (hopper:nat:max child-depth before after))
      (hopper:depth:step before child after)))
(: hopper:depth:f
  (-> (tree : hopper:tree) (depth : hopper:nat) (u 0)))
(: hopper:depth:proof
  (-> (tree : hopper:tree) (root : hopper:nat)
      (children : (list hopper:tree))
      (maximum : hopper:nat) (depth : hopper:nat)
      (view-evidence : (hopper:tree:view tree root children))
      (fold-evidence :
        (rel:fold hopper:tree hopper:nat hopper:depth:step
          hopper:nat:n0 children maximum))
      (successor-evidence : (hopper:nat:succ maximum depth))
      (hopper:depth:f tree depth)))

(: hopper:is-branch:f
  (-> (tree : hopper:tree) (path : (list hopper:nat)) (u 0)))
(: hopper:is-branch:any-child
  (-> (remaining : (list hopper:tree))
      (path : (list hopper:nat)) (u 0)))
(: hopper:is-branch:any-child-proof
  (-> (remaining : (list hopper:tree)) (child : hopper:tree)
      (path : (list hopper:nat))
      (path-tail : (list hopper:nat))
      (child-evidence :
        (rel:list:head hopper:tree remaining child))
      (tail-evidence :
        (rel:list:tail hopper:nat path path-tail))
      (branch-evidence :
        (hopper:is-branch:f child path-tail))
      (hopper:is-branch:any-child remaining path)))
(: hopper:is-branch:proof:leaf
  (-> (root : hopper:nat) (path : (list hopper:nat))
      (path-tail : (list hopper:nat))
      (view-evidence :
        (hopper:tree:view
          (hopper:tree:node root (list:nil hopper:tree))
          root (list:nil hopper:tree)))
      (tail-evidence :
        (rel:list:tail hopper:nat path path-tail))
      (empty-evidence :
        (rel:list:empty hopper:nat path-tail))
      (head-evidence : (rel:list:head hopper:nat path root))
      (hopper:is-branch:f
        (hopper:tree:node root (list:nil hopper:tree)) path)))
(: hopper:is-branch:proof:node
  (-> (root : hopper:nat) (first : hopper:tree)
      (rest : (list hopper:tree)) (path : (list hopper:nat))
      (view-evidence :
        (hopper:tree:view
          (hopper:tree:node root
            (list:cons hopper:tree first rest))
          root (list:cons hopper:tree first rest)))
      (head-evidence : (rel:list:head hopper:nat path root))
      (any-evidence :
        (rel:any hopper:tree (list hopper:nat)
          hopper:is-branch:any-child
          (list:cons hopper:tree first rest) path))
      (hopper:is-branch:f
        (hopper:tree:node root
          (list:cons hopper:tree first rest)) path)))

(: hopper:is-subtree:f
  (-> (source : hopper:tree) (target : hopper:tree) (u 0)))
(: hopper:is-subtree:any-child
  (-> (remaining : (list hopper:tree)) (target : hopper:tree)
      (u 0)))
(: hopper:is-subtree:any-child-proof
  (-> (remaining : (list hopper:tree)) (child : hopper:tree)
      (target : hopper:tree)
      (child-evidence :
        (rel:list:head hopper:tree remaining child))
      (subtree-evidence : (hopper:is-subtree:f child target))
      (hopper:is-subtree:any-child remaining target)))
(: hopper:is-subtree:proof:equal
  (-> (tree : hopper:tree) (hopper:is-subtree:f tree tree)))
(: hopper:is-subtree:proof:child
  (-> (root : hopper:nat) (children : (list hopper:tree))
      (target : hopper:tree)
      (view-evidence :
        (hopper:tree:view
          (hopper:tree:node root children) root children))
      (any-evidence :
        (rel:any hopper:tree hopper:tree
          hopper:is-subtree:any-child children target))
      (hopper:is-subtree:f
        (hopper:tree:node root children) target)))
