!(import! &self pat)
; Premise topology follows the existing PLN induction/abduction schemas.
; Matching selects evidence; the imported truth functions compute its STV.
; The existing HE and PeTTa libraries use different confidence formulas.
; This example preserves each library's output and shows all its inputs.
(evidence ca (Inheritance common A) (stv 0.5 0.5))
(evidence ca (Inheritance common A) (stv 0.5 0.5))
(evidence cb (Inheritance common B) (stv 0.5 0.75))
(evidence ac (Inheritance A effect) (stv 0.5 0.5))
(evidence bc (Inheritance B effect) (stv 0.5 0.75))
; This open fact must not acquire common to manufacture an induction premise.
(evidence open (Inheritance $unknown A) (stv 0.9 0.9))
(prior A (stv 0.5 0.1))
(prior B (stv 0.75 0.2))
(prior common (stv 0.5 0.3))
(prior effect (stv 0.5 0.3))

(= (select-induction $kb $a $b)
   (pat:match% $kb
     (, (evidence $first (Inheritance $common $a) $t1)
        (evidence $second (Inheritance $common $b) $t2)
        (prior $a $pa) (prior $b $pb) (prior $common $pc))
     (selected (Inheritance $a $b)
               ($first $second) ($pa $pb $pc) ($t1 $t2))))
(= (select-abduction $kb $a $b)
   (pat:match% $kb
     (, (evidence $first (Inheritance $a $common) $t1)
        (evidence $second (Inheritance $b $common) $t2)
        (prior $a $pa) (prior $b $pb) (prior $common $pc))
     (selected (Inheritance $a $b)
               ($first $second) ($pa $pb $pc) ($t1 $t2))))

(= (induce (selected $conclusion $ids ($pa $pb $pc) ($t1 $t2)))
   (inference induction $conclusion (evidence $ids)
     (inputs ($pa $pb $pc) ($t1 $t2))
     (Truth_Induction $pa $pb $pc $t1 $t2)))
(= (abduce (selected $conclusion $ids ($pa $pb $pc) ($t1 $t2)))
   (inference abduction $conclusion (evidence $ids)
     (inputs ($pa $pb $pc) ($t1 $t2))
     (Truth_Abduction $pa $pb $pc $t1 $t2)))
