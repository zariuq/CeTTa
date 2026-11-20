; Relations with several answers, compiled to procedures that give each answer
; to a success continuation: every element of a list (Member:01), every split
; of a list in two (Split through Append in mode 001, two outputs), ordered
; pairs of different elements (two nondet calls then a test), a nondet call
; followed by a det one (Wrapped), a refutable target on each answer (Twice:
; elements met again later), and a semidet relation whose overlapping rules
; are told apart by a call without rules (Last).
(gslt-presentation-v1 GsltCNondetV1
  (signature
    (operator cons 2)
    (operator nil 0)
    (operator Member 2)
    (operator Append 3)
    (operator Split 2)
    (operator Pairs 2)
    (operator Pair 2)
    (operator Wrapped 2)
    (operator Boxed 2)
    (operator Box 1)
    (operator Twice 2)
    (operator Last 2))
  (equations)
  (rewrites
    (rule member-head
      (head (Member ?x (cons ?x ?t)))
      (body))
    (rule member-tail
      (head (Member ?x (cons ?y ?t)))
      (body (Member ?x ?t)))
    (rule append-nil
      (head (Append nil ?y ?y))
      (body))
    (rule append-cons
      (head (Append (cons ?h ?t) ?y (cons ?h ?r)))
      (body (Append ?t ?y ?r)))
    (rule split
      (head (Split ?l (Pair ?a ?b)))
      (body (Append ?a ?b ?l)))
    (rule pairs
      (head (Pairs ?l (Pair ?a ?b)))
      (body (Member ?a ?l) (Member ?b ?l) (different ?a ?b)))
    (rule wrapped
      (head (Wrapped ?l ?w))
      (body (Member ?a ?l) (Boxed ?a ?w)))
    (rule boxed
      (head (Boxed ?a (Box ?a)))
      (body))
    (rule twice
      (head (Twice ?l ?a))
      (body (Append ?pre (cons ?a ?post) ?l) (Member ?a ?post)))
    (rule last-one
      (head (Last (cons ?x nil) ?x))
      (body))
    (rule last-more
      (head (Last (cons ?y ?t) ?x))
      (body (Last ?t ?x)))))
