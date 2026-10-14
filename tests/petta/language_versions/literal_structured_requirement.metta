; A number against a parameterized foreign type is not decided by its shape.
(: Heap (Foreign 1))
(: heap-size (-[det]-> (Heap Number) Number))
(= (heap-size $_heap) 0)
(: wrong-heap-call (-[det]-> Number))
(= (wrong-heap-call) (heap-size 42))
!(test 1 1)
