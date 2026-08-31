; A source revision change must invalidate every shared derived decision.
(= (repo-revision $x) old)
(= (repo-revision probe) exact)

(= (repo-revision-exercise $n)
   (if (== $n 0)
       (collapse (repo-revision probe))
       (let $_ (collapse (repo-revision probe))
         (if (== $n 2)
             (let $_ (add-atom &self (= (repo-revision probe) new))
               (repo-revision-exercise (- $n 1)))
             (repo-revision-exercise (- $n 1))))))

!(repo-revision-exercise 4)
