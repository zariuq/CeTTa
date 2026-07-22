!(let $p1 (delay one)
   (let $p2 (delay two)
     (let $h (car-atom $p1)
       (force ($h (__prime_need_ref_v1 1 2))))))
