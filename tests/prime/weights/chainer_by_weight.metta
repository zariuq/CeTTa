!(import! &self ../../../lib/native_weight_algebras.metta)
!(pragma! prefer-rationals True)

; A weighted backward chainer: why is the grass wet?  Each rule carries the
; strength of its step; establishing a premise costs real work.  A derivation
; weighs the product of its steps: rain 9/10 * 6/10 = 27/50 is the best,
; hose 1/2 * 99/100 = 99/200 is second, dew and flood weigh 1/40 each.
(= (slow $n $v) (if (== $n 0) $v (slow (- $n 1) $v)))

(=w (explain wet) (Via dew (explain dew)) 1/20)
(=w (explain wet) (Via flood (explain flood)) 1/20)
(=w (explain wet) (Via hose (explain hose)) 1/2)
(=w (explain wet) (Via rain (explain rain)) 9/10)

(=w (explain dew) (slow 300 (Seen cold-night)) 1/2)
(=w (explain flood) (slow 300 (Seen river)) 1/2)
(=w (explain hose) (slow 300 (Seen tap-open)) 99/100)
(=w (explain rain) (slow 300 (Seen clouds)) 6/10)

; Arrival order.
!(select 1 (search-policy (control FIFO (budget 10000000)))
   (weight:read (weights:rational) (explain wet)))

; Greedy on the factor of one step: the last factor decides.
!(select 1 (search-policy (control WO (key $body $weight:factor) (order descending)
     (budget 10000000)))
   (weight:read (weights:rational) (explain wet)))

; Best first by the accumulated coefficient, under its declared law.
!(select 1 (search-policy (control WO (order by-weight descending)
     (law unit-interval-product) (budget 10000000)))
   (weight:read (weights:rational) (explain wet)))

; The same order without a law: no pruning, the search enumerates.
!(select 1 (search-policy (control WO (order by-weight descending) (budget 10000000)))
   (weight:read (weights:rational) (explain wet)))

; A key over the authored body that reads the accumulated coefficient.
!(select 1 (search-policy (control WO (key (Via $cause $premise) $weight:coefficient)
     (order descending) (budget 10000000)))
   (weight:read (weights:rational) (explain wet)))

; Every derivation, best first, and every derivation in arrival order.
!(select 10 (search-policy (control WO (order by-weight descending)
     (law unit-interval-product) (budget 10000000)))
   (weight:read (weights:rational) (explain wet)))
!(select 10 (search-policy (control FIFO (budget 10000000)))
   (weight:read (weights:rational) (explain wet)))
