; HE-backed grounded atom ops: min-atom and max-atom compute over expression
; contents and keep float output surface.

!(min-atom (5 4 5.5))
(= (nums) (5 4 5.5))
!(min-atom (nums))
!(max-atom (5 4 5.5))
(= (nums2) (5 4 5.5))
!(max-atom (nums2))
