; Pure goals for native select. Routes have the form (Route id coverage cost).
; These equations inspect completed witnesses; select owns all pending work.

(= (search:member? $value $items)
   (if (== $items ()) False
     (let ($head $tail) (decons-atom $items)
       (if (== $value $head) True (search:member? $value $tail)))))

(= (search:route-present? $id $routes)
   (if (== $routes ()) False
     (let ((Route $head $coverage $cost) $tail) (decons-atom $routes)
       (if (== $id $head) True (search:route-present? $id $tail)))))

(= (search:covered? $obligation $routes)
   (if (== $routes ()) False
     (let ((Route $id $coverage $cost) $tail) (decons-atom $routes)
       (if (search:member? $obligation $coverage) True
         (search:covered? $obligation $tail)))))

(= (search:covers-all? $required $routes)
   (if (== $required ()) True
     (let ($head $tail) (decons-atom $required)
       (if (search:covered? $head $routes)
         (search:covers-all? $tail $routes) False))))

(= (search:compatible? $forbidden $routes)
   (if (== $forbidden ()) True
     (let (($a $b) $tail) (decons-atom $forbidden)
       (if (and (search:route-present? $a $routes)
                (search:route-present? $b $routes)) False
         (search:compatible? $tail $routes)))))

(= (search:joint? $required $forbidden $routes)
   (and (search:covers-all? $required $routes)
        (search:compatible? $forbidden $routes)))
