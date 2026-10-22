(: spine-append (-> Atom Atom %Undefined%))
(= (spine-append $left $right)
   (case $left
     ((End $right)
      ((Link $head $tail)
       (let $joined (spine-append $tail $right)
         (Link $head $joined))))))

; A near recurrence rebuilding another constructor is not append.
(: other-append (-> Atom Atom %Undefined%))
(= (other-append $left $right)
   (case $left
     ((End $right)
      ((Link $head $tail)
       (let $joined (other-append $tail $right)
         (OtherLink $head $joined))))))

!(spine-append (Link a (Link b End)) (Link c End))
!(other-append (Link a (Link b End)) (Link c End))
!(spine-append Other (Link c End))
!(once (spine-append $open (Link c End)))
