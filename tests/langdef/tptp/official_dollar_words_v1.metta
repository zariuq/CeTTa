!(import! &self ../../../lib/lib_tptp.metta)

; A fixed token of the grammar spelled like a dollar word reads as a dollar
; word wherever the grammar allows one: $let is a constant in p($let), a
; defined functor in q($let(a,b)), and the let form where its shape is the
; let shape; $ite, $fof, $cnf, $thf, $tff and $fot are constants in argument
; positions.  Both roles are in one text, under both policies, and the
; printed text reads back to the same records.
(= (dollar:text)
   "fof(a,axiom,p($let)).fof(b,axiom,q($let(a,b))).tff(c,axiom,$let(c: $i, c := a, p(c))).fof(d,axiom,r($ite) & s($fof) & t($cnf,$thf,$tff,$fot)).")

(= (dollar:run $policy)
   (let $reader (tptp:open-reader strict)
     (let $records (tptp:read-text $reader $policy (dollar:text))
       (let $text (tptp:print $reader $records)
         (let $again (tptp:read-text $reader $policy $text)
           (TptpDollarWordsV1 $policy $records $text
             (if (== $records $again) same different)))))))

!(println! (dollar:run mixed))
!(println! (dollar:run tagged))
