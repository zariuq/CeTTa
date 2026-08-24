; Structural naturals and recursive evidence for two Popper tasks.
; Parity and decrement are defined for every constructor-shaped natural, not
; only for the finite values occurring in the benchmark corpus.

(: popper:nat (u 0))
(: popper:nat:zero popper:nat)
(: popper:nat:succ (-> (before : popper:nat) popper:nat))

(: popper:nat:even (-> (value : popper:nat) (u 0)))
(: popper:nat:even:zero
  (popper:nat:even popper:nat:zero))
(: popper:nat:even:step
  (-> (before : popper:nat)
      (before-evidence : (popper:nat:even before))
      (popper:nat:even
        (popper:nat:succ (popper:nat:succ before)))))

(: popper:nat:one (-> (value : popper:nat) (u 0)))
(: popper:nat:one:evidence
  (popper:nat:one (popper:nat:succ popper:nat:zero)))
(: popper:nat:decrement
  (-> (current : popper:nat) (previous : popper:nat) (u 0)))
(: popper:nat:decrement:evidence
  (-> (previous : popper:nat)
      (popper:nat:decrement (popper:nat:succ previous) previous)))

(: popper:all-even:f
  (-> (values : (list popper:nat)) (u 0)))
(: popper:all-even:proof:empty
  (-> (empty-evidence :
        (rel:list:empty popper:nat (list:nil popper:nat)))
      (popper:all-even:f (list:nil popper:nat))))
(: popper:all-even:proof:step
  (-> (head : popper:nat) (tail : (list popper:nat))
      (head-evidence :
        (rel:list:head popper:nat
          (list:cons popper:nat head tail) head))
      (tail-evidence :
        (rel:list:tail popper:nat
          (list:cons popper:nat head tail) tail))
      (even-evidence : (popper:nat:even head))
      (recursive-evidence : (popper:all-even:f tail))
      (popper:all-even:f (list:cons popper:nat head tail))))

(: popper:drop-k:f
  (-> (source : (list popper:nat)) (amount : popper:nat)
      (result : (list popper:nat)) (u 0)))
(: popper:drop-k:proof:one
  (-> (source : (list popper:nat)) (result : (list popper:nat))
      (one-evidence :
        (popper:nat:one (popper:nat:succ popper:nat:zero)))
      (tail-evidence : (rel:list:tail popper:nat source result))
      (popper:drop-k:f source
        (popper:nat:succ popper:nat:zero) result)))
(: popper:drop-k:proof:step
  (-> (source : (list popper:nat))
      (amount : popper:nat) (previous : popper:nat)
      (intermediate : (list popper:nat))
      (result : (list popper:nat))
      (decrement-evidence :
        (popper:nat:decrement amount previous))
      (recursive-evidence :
        (popper:drop-k:f source previous intermediate))
      (tail-evidence :
        (rel:list:tail popper:nat intermediate result))
      (popper:drop-k:f source amount result)))
