; Each stored Edge is a separate occurrence, including equal edges.
(Edge a b 2)
(Edge a b 2)
(Edge a c 5)
(Edge b d 3)
(Edge c d 3)

(= (routes)
   (let $mid (match &self (Edge a $next $first-grade)
                       $next (where $first-grade))
     (match &self (Edge $mid d $second-grade)
                  (Route a $mid d) (where $second-grade))))

!(weight:read (algebra 0 1 + *) (routes))
!(select 7 (search-policy (control FIFO (budget 8192) (workers 2)))
   (weight:read (algebra 0 1 + *) (routes)))
