!(import! &self ../../../lib/lib_tptp.metta)

(= (utilities-v1:record-summary $record)
   (TptpRecordSummary
     (tptp:record-family $record)
     (tptp:record-name $record)
     (tptp:record-role $record)))
(= (utilities-v1:summaries (tptp-rec:inputs-nil)) LNil)
(= (utilities-v1:summaries (tptp-rec:inputs-cons $head $tail))
   (LCons (utilities-v1:record-summary $head)
     (utilities-v1:summaries $tail)))
(= (utilities-v1:file-summaries $file)
   (utilities-v1:summaries (tptp:records $file)))
(= (utilities-v1:first (tptp-rec:inputs-cons $head $_tail)) $head)
(= (utilities-v1:second
      (tptp-rec:inputs-cons $_first
        (tptp-rec:inputs-cons $second $_tail)))
   $second)

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
             (tptp:select-name
               (tptp-rec:name-word (tptp-rec:word-lower "a")) $file))
           (utilities-v1:file-summaries
             (tptp:select-role (tptp-rec:role "axiom") $file))
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
