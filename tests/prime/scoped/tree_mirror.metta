; Binary trees with two recursive positions: the derived induction principle
; carries two hypotheses in the node case, and mirroring is an involution.
!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: elem (u 0))))
!(set:inductive &thy tree (u 0) (: leaf tree) (: node (-> tree elem tree tree)))
!(pf:equality &thy tree refl@tree subst@tree)
!(set:known-proposition &thy tree-ind)
!(set:define &thy mirror (-> tree tree)
   (= (mirror leaf) leaf)
   (= (mirror (node $l $x $r)) (node (mirror $r) $x (mirror $l))))
; mirror (mirror t) = t: the node case rewrites both subtrees by their
; hypotheses, one congruence each.
!(pf:theorem &thy mirror-mirror (all tree (lam t (eq tree (mirror (mirror t)) t)))
   (pf:induction tree-ind (lam t (eq tree (mirror (mirror t)) t))
     (pf:refl tree leaf)
     (pf:fix l (pf:fix x (pf:fix r (pf:assume ihl (pf:assume ihr
       (pf:calc tree (mirror (mirror (node l x r)))
         (= (node l x (mirror (mirror r)))  (cong tree (lam s (node s x (mirror (mirror r)))) (mirror (mirror l)) l ihl))
         (= (node l x r)                    (cong tree (lam s (node l x s)) (mirror (mirror r)) r ihr))))))))))
; A definition that is not structural is refused: the recursion is on the
; mirrored subtree, which is not a constructor argument.
!(set:define &thy spin (-> tree tree)
   (= (spin leaf) leaf)
   (= (spin (node $l $x $r)) (spin (mirror $l))))
!(pf:theory &thy ((: a elem) (: b elem)))
!(pf:proves &thy (pf:refl tree (node leaf a (node leaf b leaf)))
   (eq tree (mirror (node (node leaf b leaf) a leaf)) (node leaf a (node leaf b leaf))))
!(set:proves &thy (pf:by mirror-mirror) (eq tree (mirror (mirror (node leaf a leaf))) (node leaf a leaf)))
