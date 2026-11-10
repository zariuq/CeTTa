; Tail calls as jumps: a loop copying its input into its own list front to
; back, through a hole in each cell (Copy); two modes calling each other
; with the answer passed through (Even, Odd); a self-call with its outputs
; swapped (Swap); an output the caller drops while another fills a hole
; (Alt, Alt2); a search whose tail call is its last rule (Find, guarded by
; different).  Not jumps: a call whose output is matched after it returns
; (HeadOf), and a tail call in a rule a later rule may follow (Look).
(gslt-presentation-v1 NativeDirectTailV1
  (signature
    (operator Copy 2)
    (operator Even 2)
    (operator Odd 2)
    (operator Swap 3)
    (operator Alt 2)
    (operator Alt2 3)
    (operator Find 3)
    (operator Look 3)
    (operator HeadOf 2)
    (operator Cell 2)
    (operator End 0)
    (operator S 1)
    (operator Z 0)
    (operator A 0)
    (operator B 0)
    (operator Yes 0)
    (operator No 0)
    (operator cons 2)
    (operator nil 0))
  (equations)
  (rewrites
    (rule copy-nil
      (head (Copy nil End))
      (body))
    (rule copy-cons
      (head (Copy (cons ?x ?r) (Cell ?x ?out)))
      (body (Copy ?r ?out)))
    (rule even-nil
      (head (Even nil Yes))
      (body))
    (rule even-cons
      (head (Even (cons ?x ?r) ?a))
      (body (Odd ?r ?a)))
    (rule odd-nil
      (head (Odd nil No))
      (body))
    (rule odd-cons
      (head (Odd (cons ?x ?r) ?a))
      (body (Even ?r ?a)))
    (rule swap-nil
      (head (Swap nil A B))
      (body))
    (rule swap-cons
      (head (Swap (cons ?x ?r) ?u ?v))
      (body (Swap ?r ?v ?u)))
    (rule alt-nil
      (head (Alt nil Z))
      (body))
    (rule alt-cons
      (head (Alt (cons ?x ?r) (S ?n)))
      (body (Alt2 ?r ?n ?d)))
    (rule alt2-nil
      (head (Alt2 nil Z No))
      (body))
    (rule alt2-cons
      (head (Alt2 (cons ?x ?r) ?n Yes))
      (body (Alt ?r ?n)))
    (rule find-nil
      (head (Find nil ?k No))
      (body))
    (rule find-here
      (head (Find (cons ?x ?r) ?x Yes))
      (body))
    (rule find-on
      (head (Find (cons ?x ?r) ?k ?a))
      (body (different ?x ?k) (Find ?r ?k ?a)))
    (rule look-here
      (head (Look (cons ?x ?r) ?x Yes))
      (body))
    (rule look-on
      (head (Look (cons ?x ?r) ?k ?a))
      (body (different ?x ?k) (Look ?r ?k ?a)))
    (rule look-nil
      (head (Look nil ?k No))
      (body))
    (rule head-of
      (head (HeadOf (cons ?x ?r) ?y))
      (body (Copy ?r (Cell ?y ?rest))))))
