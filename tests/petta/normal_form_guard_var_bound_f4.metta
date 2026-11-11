; Admission by normal form keeps its guards (a let's variable bound by its
; own bound expression compares; a case whose first-arm variable is bound
; falls through to its next arm; an arm written Empty is the case's own).
(= (f $v) (case 3 (($v 10) ($w 20))))
!(f 4)
