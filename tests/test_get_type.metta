; get-type operation
(: x Number)
(: f (-> Number Number))
!(assertEqual (get-type x) Number)
!(assertEqual (get-type 42) Number)
!(assertEqual (get-type "hello") String)
!(assertEqual (get-type f) (-> Number Number))
