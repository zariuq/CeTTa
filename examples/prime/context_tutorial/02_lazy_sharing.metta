; Chapter 2: a binding is lazy and shared
;
; `ctx:bind` allocates one hidden Need cell for its value.  Every lookup of that
; binding in one branch reaches the same cell.  This is call-time choice: one
; unknown is chosen once, then all consumers agree.

!(let $shared
     (ctx:bind ctx:empty weather (superpose (sun rain)))
   (Triple
     (ctx:get $shared weather)
     (ctx:get $shared weather)
     (ctx:get $shared weather)))

; Textually equal origins do not imply shared identity.  Two `ctx:bind` calls
; allocate two cells, so their choices are independent.
!(let $first
     (ctx:bind ctx:empty weather (superpose (sun rain)))
   (let $second
       (ctx:bind ctx:empty weather (superpose (sun rain)))
     (Pair
       (ctx:get $first weather)
       (ctx:get $second weather))))

; Passing a context through another function does not duplicate its cells.
(= (ctx-tutorial:three-readers $context $key)
   (Triple
     (ctx:get $context $key)
     (ctx:get $context $key)
     (ctx:get $context $key)))
!(let $shared
     (ctx:bind ctx:empty route (superpose (north south)))
   (ctx-tutorial:three-readers $shared route))

; Missing data is also stable under repeated lookup; it is not a fault branch.
!(let $context (ctx:bind ctx:empty known present)
   (Pair
     (ctx:get $context unknown)
     (ctx:get $context unknown)))
