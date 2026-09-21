!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

(= (print-real:input $input)
   (case $input
     (((tptp-rec:fof $n $r $f $a $_s)
        (tptp-rec:fof $n $r $f $a (tptp-rec:span 0 0)))
      ((tptp-rec:cnf $n $r $f $a $_s)
        (tptp-rec:cnf $n $r $f $a (tptp-rec:span 0 0)))
      ((tptp-rec:tff $n $r $f $a $_s)
        (tptp-rec:tff $n $r $f $a (tptp-rec:span 0 0)))
      ((tptp-rec:thf $n $r $f $a $_s)
        (tptp-rec:thf $n $r $f $a (tptp-rec:span 0 0)))
      ((tptp-rec:tcf $n $r $f $a $_s)
        (tptp-rec:tcf $n $r $f $a (tptp-rec:span 0 0)))
      ((tptp-rec:tpi $n $r $f $a $_s)
        (tptp-rec:tpi $n $r $f $a (tptp-rec:span 0 0)))
      ((tptp-rec:include $f $sel $q $_s)
        (tptp-rec:include $f $sel $q (tptp-rec:span 0 0))))))
(= (print-real:inputs (tptp-rec:inputs-nil)) (tptp-rec:inputs-nil))
(= (print-real:inputs (tptp-rec:inputs-cons $head $tail))
   (tptp-rec:inputs-cons
     (print-real:input $head) (print-real:inputs $tail)))
(= (print-real:normalize (tptp-rec:file $inputs $_span))
   (tptp-rec:file (print-real:inputs $inputs) (tptp-rec:span 0 0)))

!(let $records (tptp:read-file (system:arg 0))
   (let $reparsed (tptp:read-text (tptp:print-records $records))
     (println!
       (TptpCanonicalPrintRealV1
         (== (print-real:normalize $records)
             (print-real:normalize $reparsed))
         (tptp:record-statistics $reparsed)))))
