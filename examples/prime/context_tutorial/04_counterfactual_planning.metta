; Chapter 4: counterfactual planning over shared uncertain premises
;
; A context can hold a hypothetical world.  Competing policies may inspect the
; same uncertain facts without accidentally choosing a different world for each
; policy.  This is useful for fair comparison of plans or explanations.

(= (ctx-plan:assess $world $policy)
   (assessment $policy
     (ctx:get $world terrain)
     (ctx:get $world weather)))

; Each policy sees the same terrain and weather chosen for its branch.  There
; are four worlds, not sixteen independently resampled policy observations.
!(let $terrain-world
     (ctx:bind ctx:empty terrain (superpose (clear muddy)))
   (let $world
       (ctx:bind $terrain-world weather (superpose (dry rain)))
     (Pair
       (ctx-plan:assess $world cautious)
       (ctx-plan:assess $world bold))))

; Counterfactual children can disagree while retaining a common base.
!(let $base (ctx:bind ctx:empty budget medium)
   (let $north-world (ctx:bind $base route north)
     (let $south-world (ctx:bind $base route south)
       (Triple
         (ctx:get $base route)
         (ctx:get $north-world route)
         (ctx:get $south-world route)))))

; Values can be passed to a generic consumer.  No global context is implied.
(= (ctx-plan:summary $world)
   (world-summary
     (ctx:get $world budget)
     (ctx:get $world route)))
!(let $base (ctx:bind ctx:empty budget low)
   (ctx-plan:summary (ctx:bind $base route local)))
