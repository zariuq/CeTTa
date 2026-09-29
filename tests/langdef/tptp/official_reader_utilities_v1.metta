!(import! &self ../../../lib/lib_tptp.metta)

(= (utilities-v1:record-summary $record)
   (TptpRecordSummary
     (tptp:record-family $record)
     (tptp:record-name $record)
     (tptp:record-role $record)))
(= (utilities-v1:summaries $records)
   (if (== (size-atom $records) 0)
       LNil
       (let $head (car-atom $records)
         (let $tail (cdr-atom $records)
           (LCons (utilities-v1:record-summary $head)
             (utilities-v1:summaries $tail))))))
(= (utilities-v1:file-summaries $file)
   (utilities-v1:summaries (tptp:records $file)))
(= (utilities-v1:first $records) (car-atom $records))
(= (utilities-v1:second $records)
   (let $rest (cdr-atom $records) (car-atom $rest)))

(= (utilities-v1:run $file)
   (let $inputs (tptp:records $file)
     (let $first (utilities-v1:first $inputs)
       (let $include (utilities-v1:second $inputs)
         (TptpOfficialReaderUtilitiesV1
           (tptp:record-count $file)
           (tptp:record-statistics $file)
           (utilities-v1:file-summaries $file)
           (utilities-v1:file-summaries (tptp:select-family fof $file))
           (utilities-v1:file-summaries
             (tptp:select-name a $file))
           (utilities-v1:file-summaries
             (tptp:select-role axiom $file))
           (tptp:record-formula $first)
           (tptp:record-span $first)
           (TptpIncludeFields
             (tptp:record-name $include)
             (tptp:record-role $include)
             (tptp:record-formula $include)
             (tptp:record-annotation $include))
           (tptp:record-statistics (tptp:select-family thf $file))
           (tptp:record-family (not-a-tptp-record)))))))

!(println!
   (utilities-v1:run
     (tptp:read-text
       "fof(a,axiom,p). include('Axioms/A.ax',[a]). cnf(a,conjecture,~q). fof(b,axiom,r).")))
