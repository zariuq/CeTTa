!(import! &self "../tools/tcalc_r2_scoped_rules.metta")
!(bind! &rules (new-space))
!(let $source (r2-schema-text
    (rule 4 a1i (holes ((v 0) 1 free) ((v 1) 1 free))
       (take) (keep (0 (v 1))) (tell) (neq) (absent) (count) (op) (guard) (fresh)
       (inputs ((v 0))) (birth 0) (make (0 (c0-im (v 0) (v 1))))))
   (let $opened (parse $source) (add-atom &rules $opened)))
!(println! (let $answers (collapse (match &rules
    (rule 4 a1i (holes (A 1 free) (B 1 free))
       (take) (keep (0 B)) (tell) (neq) (absent) (count) (op) (guard) (fresh)
       (inputs (A)) (birth 0) (make (0 (c0-im A B)))) accepted))
   (consistent-frame $answers)))
!(println! (let $answers (collapse (match &rules
    (rule 4 a1i (holes (A 1 free) (B 1 free))
       (take) (keep (0 C)) (tell) (neq) (absent) (count) (op) (guard) (fresh)
       (inputs (A)) (birth 0) (make (0 (c0-im A B)))) accepted))
   (different-hypothesis $answers)))
!(println! (let $answers (collapse (match &rules
    (rule 4 a1i (holes (A 1 free) (B 1 free))
       (take) (keep (0 B)) (tell) (neq) (absent) (count) (op) (guard) (fresh)
       (inputs (B)) (birth 0) (make (0 (c0-im A B)))) accepted))
   (different-input $answers)))
!(let $source (r2-schema-text
    (rule 5 apart (holes ((v 0) 1 (bound ((v 1)))) ((v 1) 1 free))
       (take) (keep (0 (v 1))) (tell) (neq) (absent) (count) (op) (guard) (fresh)
       (inputs ((v 0))) (birth 0) (make (0 (c0-im (v 0) (v 1))))))
   (let $opened (parse $source) (add-atom &rules $opened)))
!(println! (let $answers (collapse (match &rules
    (rule 5 apart (holes (A 1 (bound (B))) (B 1 free))
       (take) (keep (0 B)) (tell) (neq) (absent) (count) (op) (guard) (fresh)
       (inputs (A)) (birth 0) (make (0 (c0-im A B)))) accepted))
   (consistent-avoidance $answers)))
!(println! (let $answers (collapse (match &rules
    (rule 5 apart (holes (A 1 (bound (A))) (B 1 free))
       (take) (keep (0 B)) (tell) (neq) (absent) (count) (op) (guard) (fresh)
       (inputs (A)) (birth 0) (make (0 (c0-im A B)))) accepted))
   (different-avoidance $answers)))
