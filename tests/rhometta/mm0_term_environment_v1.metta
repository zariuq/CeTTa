; A plain sort followed by a nullary term declaration is accepted.
!(gslt-rho:query:MM0TextualEnvironmentFromAstV1
   QueryMM0TermEnvironmentSimpleV1
   (MM0TextualEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-sort-statement
           (pair none
             (pair none
               (pair none
                 (pair none
                   (node mm0-identifier
                     (pair (cp 115) nil)))))))
         (cons
           (node mm0-term-statement
             (pair
               (node mm0-identifier (pair (cp 97) nil))
               (pair nil
                 (node mm0-arrow-type
                   (pair
                     (node mm0-type
                       (pair
                         (node mm0-identifier (pair (cp 115) nil))
                         nil))
                     none)))))
           nil)))
     MM0AcceptedV1 $simple-environment $simple-history))

; A bound variable may support a later regular binder dependency.
!(gslt-rho:query:MM0TextualEnvironmentFromAstV1
   QueryMM0TermEnvironmentDependentV1
   (MM0TextualEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-sort-statement
           (pair none
             (pair none
               (pair none
                 (pair none
                   (node mm0-identifier
                     (pair (cp 115)
                       (cons (cp 101) (cons (cp 116) nil)))))))))
         (cons
           (node mm0-sort-statement
             (pair none
               (pair (some strict-source-v1)
                 (pair (some provable-source-v1)
                   (pair none
                     (node mm0-identifier
                       (pair (cp 119)
                         (cons (cp 102) (cons (cp 102) nil)))))))))
           (cons
             (node mm0-term-statement
               (pair
                 (node mm0-identifier
                   (pair (cp 97) (cons (cp 108) (cons (cp 108) nil))))
                 (pair
                   (cons
                     (node mm0-type-binder-bound
                       (pair
                         (cons
                           (node mm0-identifier (pair (cp 120) nil))
                           nil)
                         (node mm0-type
                           (pair
                             (node mm0-identifier
                               (pair (cp 115)
                                 (cons (cp 101) (cons (cp 116) nil))))
                             nil))))
                     (cons
                       (node mm0-type-binder-regular
                         (pair
                           (cons
                             (node mm0-identifier
                               (pair (cp 112) (cons (cp 104) nil)))
                             nil)
                           (node mm0-type
                             (pair
                               (node mm0-identifier
                                 (pair (cp 119)
                                   (cons (cp 102) (cons (cp 102) nil))))
                               (cons
                                 (node mm0-identifier (pair (cp 120) nil))
                                 nil)))))
                       nil))
                   (node mm0-arrow-type
                     (pair
                       (node mm0-type
                         (pair
                           (node mm0-identifier
                             (pair (cp 119)
                               (cons (cp 102) (cons (cp 102) nil))))
                           nil))
                       none)))))
             nil))))
     MM0AcceptedV1 $dependent-environment $dependent-history))

; A term cannot return a pure sort.
!(gslt-rho:query:MM0TextualEnvironmentFromAstV1
   QueryMM0TermEnvironmentPureTargetV1
   (MM0TextualEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-sort-statement
           (pair (some pure-source-v1)
             (pair none
               (pair none
                 (pair none
                   (node mm0-identifier
                     (pair (cp 115)
                       (cons (cp 101) (cons (cp 116) nil)))))))))
         (cons
           (node mm0-term-statement
             (pair
               (node mm0-identifier
                 (pair (cp 99)
                   (cons (cp 111)
                     (cons (cp 110)
                       (cons (cp 115)
                         (cons (cp 116)
                           (cons (cp 97)
                             (cons (cp 110) (cons (cp 116) nil)))))))))
               (pair nil
                 (node mm0-arrow-type
                   (pair
                     (node mm0-type
                       (pair
                         (node mm0-identifier
                           (pair (cp 115)
                             (cons (cp 101) (cons (cp 116) nil))))
                         nil))
                     none)))))
           nil)))
     (MM0RefusedPureTermTargetV1
       (NatSuccV1 NatZeroV1)
       (cons (cp 99)
         (cons (cp 111)
           (cons (cp 110)
             (cons (cp 115)
               (cons (cp 116)
                 (cons (cp 97)
                   (cons (cp 110) (cons (cp 116) nil))))))))
       (cons (cp 115) (cons (cp 101) (cons (cp 116) nil))))
     $pure-environment $pure-history))

; The pure-target source cannot also produce an accepted result.
!(gslt-rho:query:MM0TextualEnvironmentFromAstV1
   QueryMM0TermEnvironmentPureTargetAcceptedV1
   (MM0TextualEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-sort-statement
           (pair (some pure-source-v1)
             (pair none
               (pair none
                 (pair none
                   (node mm0-identifier
                     (pair (cp 115)
                       (cons (cp 101) (cons (cp 116) nil)))))))))
         (cons
           (node mm0-term-statement
             (pair
               (node mm0-identifier
                 (pair (cp 99)
                   (cons (cp 111)
                     (cons (cp 110)
                       (cons (cp 115)
                         (cons (cp 116)
                           (cons (cp 97)
                             (cons (cp 110) (cons (cp 116) nil)))))))))
               (pair nil
                 (node mm0-arrow-type
                   (pair
                     (node mm0-type
                       (pair
                         (node mm0-identifier
                           (pair (cp 115)
                             (cons (cp 101) (cons (cp 116) nil))))
                         nil))
                     none)))))
           nil)))
     MM0AcceptedV1 $impossible-environment $impossible-history))
