; The same source can use native or PathMap storage. Increasing the stored
; rows and the repeat count separates loading from repeated scoped lookup.
(= (lookup-prefix $space $key) (match $space (edge $key $value) $value))
(= (lookup-suffix $space $key) (match $space (edge $value $key) $value))
(= (lookup-repeat $space $remaining)
   (if (== $remaining 0) 0
     (let $rows (collapse (lookup-suffix $space c))
       (+ (size-atom $rows) (lookup-repeat $space (- $remaining 1))))))

!(bind! &lookup-rows (new-space))
!(add-atom &lookup-rows (edge a b))
!(add-atom &lookup-rows (edge a c))
!(add-atom &lookup-rows (edge b c))
!(add-atom &lookup-rows (edge b c))
!(lookup-repeat &lookup-rows 10)
