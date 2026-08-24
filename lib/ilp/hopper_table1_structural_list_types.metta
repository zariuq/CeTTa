; Typed structural-list specializations of three authored Hopper programs.
; The generic relations remain ordinary Prime values with explicit evidence.

(: hopper:last-half:case
  (-> (source : (list hopper:atom))
      (target : (list hopper:atom)) (u 0)))
(: hopper:last-half:case:nil
  (hopper:last-half:case
    (list:nil hopper:atom) (list:nil hopper:atom)))
(: hopper:last-half:case:singleton
  (-> (head : hopper:atom)
      (hopper:last-half:case
        (list:cons hopper:atom head (list:nil hopper:atom))
        (list:nil hopper:atom))))
(: hopper:last-half:case:cons
  (-> (head : hopper:atom) (next : hopper:atom)
      (rest : (list hopper:atom))
      (trimmed : (list hopper:atom))
      (recursive-result : (list hopper:atom))
      (target : (list hopper:atom))
      (front-evidence :
        (rel:list:front hopper:atom
          (list:cons hopper:atom next rest) trimmed))
      (recursive-evidence :
        (hopper:last-half:case trimmed recursive-result))
      (snoc-evidence :
        (rel:list:snoc hopper:atom recursive-result head target))
      (hopper:last-half:case
        (list:cons hopper:atom head
          (list:cons hopper:atom next rest)) target)))
(: hopper:last-half:f
  (-> (source : (list hopper:atom))
      (target : (list hopper:atom)) (u 0)))
(: hopper:last-half:proof
  (-> (source : (list hopper:atom))
      (reversed : (list hopper:atom))
      (target : (list hopper:atom))
      (reverse-evidence : (hopper:reverse:f source reversed))
      (case-evidence : (hopper:last-half:case reversed target))
      (hopper:last-half:f source target)))

(: hopper:one (-> (value : hopper:nat) (u 0)))
(: hopper:one:proof
  (-> (zero-evidence : (hopper:nat:zero hopper:nat:n0))
      (successor-evidence :
        (hopper:nat:succ hopper:nat:n0 hopper:nat:n1))
      (hopper:one hopper:nat:n1)))
(: hopper:two (-> (value : hopper:nat) (u 0)))
(: hopper:two:proof
  (-> (zero-evidence : (hopper:nat:zero hopper:nat:n0))
      (first-successor :
        (hopper:nat:succ hopper:nat:n0 hopper:nat:n1))
      (second-successor :
        (hopper:nat:succ hopper:nat:n1 hopper:nat:n2))
      (hopper:two hopper:nat:n2)))
(: hopper:of-one-and-two:f
  (-> (values : (list hopper:nat)) (u 0)))
(: hopper:of-one-and-two:nil
  (hopper:of-one-and-two:f (list:nil hopper:nat)))
(: hopper:of-one-and-two:cons
  (-> (head : hopper:nat) (tail : (list hopper:nat))
      (choice-evidence :
        (rel:either hopper:nat hopper:one hopper:two head))
      (tail-evidence : (hopper:of-one-and-two:f tail))
      (hopper:of-one-and-two:f
        (list:cons hopper:nat head tail))))

(: hopper:palindrome:f
  (-> (values : (list hopper:atom)) (u 0)))
(: hopper:palindrome:nil
  (hopper:palindrome:f (list:nil hopper:atom)))
(: hopper:palindrome:singleton
  (-> (head : hopper:atom)
      (hopper:palindrome:f
        (list:cons hopper:atom head (list:nil hopper:atom)))))
(: hopper:palindrome:cons
  (-> (head : hopper:atom) (next : hopper:atom)
      (rest : (list hopper:atom))
      (middle : (list hopper:atom))
      (last-evidence :
        (rel:list:last hopper:atom
          (list:cons hopper:atom next rest) head))
      (front-evidence :
        (rel:list:front hopper:atom
          (list:cons hopper:atom next rest) middle))
      (recursive-evidence : (hopper:palindrome:f middle))
      (hopper:palindrome:f
        (list:cons hopper:atom head
          (list:cons hopper:atom next rest)))))
