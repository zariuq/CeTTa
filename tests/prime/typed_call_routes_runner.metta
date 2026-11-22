; A runner read before the program that uses it: it applies the relation a
; meaning names, so the callee is known only when the call is reached.
(= (route:run $name $input)
   (let $relation (route:meaning $name)
     (let (route:edge $target $evidence) ($relation $input)
       (route:out $target $evidence))))
