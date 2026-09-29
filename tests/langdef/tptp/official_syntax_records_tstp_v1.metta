; TSTP samples produced locally by E and Vampire from PUZ001-1/PUZ001+1.
!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

(= (tstp-test:names $records)
   (if (== (size-atom $records) 0)
       LNil
       (let $head (car-atom $records)
         (let $tail (cdr-atom $records)
           (case (tptp:record-name $head)
             (((TPTP:RecordName $name) (LCons $name (tstp-test:names $tail)))
              ($other (LCons $other (tstp-test:names $tail)))))))))

(= (tstp-test:kinds $records)
   (if (== (size-atom $records) 0)
       LNil
       (let $head (car-atom $records)
         (let $tail (cdr-atom $records)
           (LCons (tptp:record-family $head) (tstp-test:kinds $tail))))))

(= (tstp-test:has-inference $records)
   (if (== (size-atom $records) 0)
       False
       (let $head (car-atom $records)
         (let $tail (cdr-atom $records)
           (case (tptp:record-source $head)
             (((TPTP:RecordSource (inference $_rule $_info $_parents)) True)
              ($_ (tstp-test:has-inference $tail))))))))

(= (tstp-test:row $tag $path)
   (let $records (tptp:read-file $path)
     (let $_ (println! (TptpTstpRecords $tag $records))
       (if (== (get-metatype (car-atom $records)) Expression)
           (TptpTstpRow $tag (tstp-test:kinds $records)
             (tstp-test:names $records)
             (tstp-test:has-inference $records))
           (TptpTstpRowFault $tag $records)))))

(= (tstp-test:run)
   (TptpOfficialSyntaxRecordsTstpV1Summary
     (tstp-test:row e-cnf (system:arg 0))
     (tstp-test:row e-fof (system:arg 1))
     (tstp-test:row vampire-cnf (system:arg 2))
     (tstp-test:row vampire-fof (system:arg 3))))
!(println! (tstp-test:run))
