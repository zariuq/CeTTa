!(import! &self math_space_store)
!(import! &self mathematical_workspace)
!(bind! &project (math:new "routes"))
!(math:declare &project (quote (hol:inductive N (u 0) (: z N) (: s (-> N N)))))
!(math:declare &project (quote (hol:define add (-> N N N)
   (= (add $x z) $x) (= (add $x (s $y)) (s (add $x $y))))))
!(bind! &route
   (let* (($theory (math:theory &project))
          ($wire (math:presentation $theory (N) (z s add))))
     (langdef:load-language-value $wire)))

; The operational route proposes evidence; the workspace checks its endpoints.
(= (route:adopt $project $id $result)
   (case $result
     (((Math:Steps $status
        ((Math:Step (Math:Conversion $source $target $proof $reported) $trace)))
        (let* (($arguments (cons-atom N (cons-atom $source (cons-atom $target ()))))
               ($type (cons-atom id $arguments)) ($claim (math:code $type)) ($evidence (math:code $proof)))
          (chain (math:goal $project $id type $claim "An equality proposed by an operational route.") $goal
            (chain (math:propose $project $id $id "gslt-route" $evidence) $candidate
              (chain (math:check $project $id 100000) $checked (math:attempt-status $project $id))))))
      ($other UnsupportedRouteResult))))
!(let $theory (math:theory &project)
   (assertEqual
     (route:adopt &project "step" (math:via $theory N &route "N" 8 100000 100000 (add (s (z)) (z))))
     checked))
!(assertEqual (math:publish &project "step" route-equality 100000)
   (Math:Published route-equality "step"))
!(assertEqual (math:goal-status &project "step" 100000)
   (Math:GoalStatus "step" checked (published route-equality)))
!(assertEqual
   (case (route:adopt &project "forged"
     (Math:Steps complete ((Math:Step (Math:Conversion z (s z) (refl z) (Established fabricated)) invented-trace))))
     (((rejected $reason) RejectedEvidence) ($other Wrong))) RejectedEvidence)
!(let $snapshot (math:serialize &project)
   (assertEqual
     (case (math:restore $snapshot 100000)
       (((Math:Restored $restored) (math:goal-status $restored "step" 100000)) ($other Wrong)))
     (Math:GoalStatus "step" checked (published route-equality))))
!(langdef:close-language &route)
