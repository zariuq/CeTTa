(: unwrap-quoted (-> Expression %Undefined%))

(= (unwrap-quoted (quote $value))
   (let $commit (cut) (quote (quoted $value))))
(= (unwrap-quoted $value)
   (quote (fallback $value)))

!(unwrap-quoted (quote 7))
!(unwrap-quoted 7)
!(repr (unwrap-quoted 7))
