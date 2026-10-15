; A numeric ordering consumer can refuse without consuming a completed tagged answer.
!(import! &self ../../../lib/native_weight_algebras.metta)
!(pragma! prefer-rationals True)
(=w (route) (let $_ (println! RECOVERY-BODY) V) (TropicalFinite 2))
!(let (Observation $v (satisfied $s) (closed $c) (residual $r) (work $w) (status $t) $order)
   (select 1 (search-policy (control WO (budget 20000) (workers 1)
                (order by-weight ascending) (law nonnegative-sum)))
     (weight:read (weights:tropical) (route)))
   (Recovery $w $t $order
     (select 7 (search-policy (control FIFO (budget 0) (workers 1))) $r)))
