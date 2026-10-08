; Set or multiset theta-subsumption of signed first-order clauses.
; Literals use (pos atom) or (neg atom). Equality is an ordinary predicate;
; this adapter does not add equality reasoning or reverse an equality literal.
; A target clause is one scoped term, not a space of separately freshened rows.
!(import! &self pat)
(: pat:subsume (-> Atom Atom Atom %Undefined%))
(= (pat:subsume $policy $general $specific)
   (pat:unify% set $policy
     (pat:checked-cover $policy $general $specific)
     (pat:unify% multiset $policy
       (pat:checked-cover $policy $general $specific)
       (pat:apply (pat:solve match% ()) (Error (pat:subsume $policy $general $specific) UnknownClausePolicy)))))

; Shape faults are explicit Error terms, not an empty matching frontier. Validation
; inspects syntax and does not run the atom inside a signed literal.
(: pat:clause-shape (-> Atom %Undefined%))
(= (pat:clause-shape $clause)
   (case (get-metatype $clause)
     ((Expression
       (case (size-atom $clause)
         ((0 pat:valid)
          ($n
           (let ($literal $rest) (decons-atom $clause)
             (pat:unify% (pos $atom) $literal
               (pat:clause-shape $rest)
               (pat:unify% (neg $atom) $literal
                 (pat:clause-shape $rest) pat:invalid)))))))
      ($other pat:invalid))))

(: pat:checked-cover (-> Atom Atom Atom %Undefined%))
(= (pat:checked-cover $policy $general $specific)
   (case (pat:clause-shape $general)
     ((pat:valid
       (case (pat:clause-shape $specific)
         ((pat:valid (pat:cover $policy $general $specific (($specific $specific))))
          ($bad (pat:apply (pat:solve match% ()) (Error (pat:subsume $policy $general $specific) ExpectedSignedClause))))))
      ($bad (pat:apply (pat:solve match% ()) (Error (pat:subsume $policy $general $specific) ExpectedSignedClause))))))

; The identity constraint retains every target variable as protected,
; including variables in literals that no selected alignment uses.
(: pat:cover (-> Atom Atom Atom Atom %Undefined%))
(= (pat:cover $policy $source $target $pairs)
   (case (size-atom $source)
     ((0 (pat:solve match% $pairs))
      ($n
       (let ($literal $rest) (decons-atom $source)
         (let (pat:choice $candidate $unused) (pat:pick $target)
           (let $constraints (cons-atom ($literal $candidate) $pairs)
             (let $witness (pat:solve match% $constraints)
               (case $policy
                 ((set (pat:cover $policy $rest $target $constraints))
                  (multiset (pat:cover $policy $rest $unused $constraints))))))))))))

; Each choice carries the unused occurrences. In set mode the next premise
; may reuse the original target; multiset mode consumes the selected one.
(: pat:pick (-> Atom %Undefined%))
(= (pat:pick $items)
   (case (size-atom $items)
     ((0 (empty))
      ($n
       (let ($first $rest) (decons-atom $items)
         (superpose
           ((cons-atom pat:choice ($first $rest))
            (let (pat:choice $chosen $unused) (pat:pick $rest)
              (let $remaining (cons-atom $first $unused)
                (cons-atom pat:choice ($chosen $remaining)))))))))))
