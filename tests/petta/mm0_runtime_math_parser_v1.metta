!(import! &self langdef)

(= (mm0:math:name:x) (cons-atom cons (cons-atom (cp 120) (cons-atom nil ()))))
(= (mm0:math:name:y) (cons-atom cons (cons-atom (cp 121) (cons-atom nil ()))))
(= (mm0:math:name:neg)
   (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 101) (cons-atom (cons-atom cons (cons-atom (cp 103) (cons-atom nil ()))) ()))) ()))))
(= (mm0:math:name:imp)
   (cons-atom cons (cons-atom (cp 105) (cons-atom (cons-atom cons (cons-atom (cp 109) (cons-atom (cons-atom cons (cons-atom (cp 112) (cons-atom nil ()))) ()))) ()))))
(= (mm0:math:name:app)
   (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 112) (cons-atom (cons-atom cons (cons-atom (cp 112) (cons-atom nil ()))) ()))) ()))))
(= (mm0:math:name:and)
   (cons-atom cons (cons-atom (cp 97) (cons-atom (cons-atom cons (cons-atom (cp 110) (cons-atom (cons-atom cons (cons-atom (cp 100) (cons-atom nil ()))) ()))) ()))))

(= (mm0:math:scalars:right-associative)
   (MM0MathScalarsConsV1 (cp 126)
     (MM0MathScalarsConsV1 (cp 32)
       (MM0MathScalarsConsV1 (cp 120)
         (MM0MathScalarsConsV1 (cp 32)
           (MM0MathScalarsConsV1 (cp 45)
             (MM0MathScalarsConsV1 (cp 62)
               (MM0MathScalarsConsV1 (cp 32)
                 (MM0MathScalarsConsV1 (cp 121)
                   (MM0MathScalarsConsV1 (cp 32)
                     (MM0MathScalarsConsV1 (cp 45)
                       (MM0MathScalarsConsV1 (cp 62)
                         (MM0MathScalarsConsV1 (cp 32)
                           (MM0MathScalarsConsV1
                             (cp 120) MM0MathScalarsNilV1))))))))))))))

(= (mm0:math:scalars:maximum-prefix)
   (MM0MathScalarsConsV1 (cp 64)
     (MM0MathScalarsConsV1 (cp 32)
       (MM0MathScalarsConsV1 (cp 120) MM0MathScalarsNilV1))))

(= (mm0:math:scalars:grouped)
   (MM0MathScalarsConsV1 (cp 126)
     (MM0MathScalarsConsV1 (cp 32)
       (MM0MathScalarsConsV1 (cp 40)
         (MM0MathScalarsConsV1 (cp 32)
           (MM0MathScalarsConsV1 (cp 120)
             (MM0MathScalarsConsV1 (cp 32)
               (MM0MathScalarsConsV1 (cp 45)
                 (MM0MathScalarsConsV1 (cp 62)
                   (MM0MathScalarsConsV1 (cp 32)
                     (MM0MathScalarsConsV1 (cp 121)
                       (MM0MathScalarsConsV1 (cp 32)
                         (MM0MathScalarsConsV1
                           (cp 41) MM0MathScalarsNilV1)))))))))))))

(= (mm0:math:scalars:incomplete)
   (MM0MathScalarsConsV1 (cp 120)
     (MM0MathScalarsConsV1 (cp 32)
       (MM0MathScalarsConsV1 (cp 45)
         (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1)))))

(= (mm0:math:scalars:left-associative)
   (MM0MathScalarsConsV1 (cp 120)
     (MM0MathScalarsConsV1 (cp 32)
       (MM0MathScalarsConsV1 (cp 38)
         (MM0MathScalarsConsV1 (cp 32)
           (MM0MathScalarsConsV1 (cp 121)
             (MM0MathScalarsConsV1 (cp 32)
               (MM0MathScalarsConsV1 (cp 38)
                 (MM0MathScalarsConsV1 (cp 32)
                   (MM0MathScalarsConsV1
                     (cp 120) MM0MathScalarsNilV1))))))))))

(= (mm0:math:scalars:plain-application)
   (MM0MathScalarsConsV1 (cp 105)
     (MM0MathScalarsConsV1 (cp 109)
       (MM0MathScalarsConsV1 (cp 112)
         (MM0MathScalarsConsV1 (cp 32)
           (MM0MathScalarsConsV1 (cp 120)
             (MM0MathScalarsConsV1 (cp 32)
               (MM0MathScalarsConsV1
                 (cp 121) MM0MathScalarsNilV1))))))))

(= (mm0:math:scalars:unknown-identifier)
   (MM0MathScalarsConsV1 (cp 122) MM0MathScalarsNilV1))

!(let
   (gslt:result:MM0SourceEnvironmentFromAstV1:1000
     MM0AcceptedV1
     (MM0SourceStateV1 $textual $notation)
     $history)
   (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
     (let
       (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
       (langdef:parse-file $mm0-langdef
         "tests/langdef/mm0/notation_environment_positive.mm0")
       (gslt:fn:MM0SourceEnvironmentFromAstV1:1000 $ast)))
   (let*
     (($x (mm0:math:name:x))
      ($y (mm0:math:name:y))
      ($neg (mm0:math:name:neg))
      ($imp (mm0:math:name:imp))
      ($app (mm0:math:name:app))
      ($scope
        (MM0MathScopeConsV1 $x
          (MM0MathScopeConsV1 $y MM0MathScopeNilV1))))
     (if
       (==
         (collapse
           (gslt:mode:MM0RuntimeMathParseV1:1110
             (MM0SourceStateV1 $textual $notation) $scope
             (mm0:math:scalars:right-associative)))
         ((gslt:result:MM0RuntimeMathParseV1:1110
            (GSLTPrattApplicationV1 $imp
              (GSLTPrattArgumentsConsV1
                (GSLTPrattApplicationV1 $neg
                  (GSLTPrattArgumentsConsV1
                    (GSLTPrattIdentifierApplicationV1
                      $x GSLTPrattArgumentsNilV1)
                    GSLTPrattArgumentsNilV1))
                (GSLTPrattArgumentsConsV1
                  (GSLTPrattApplicationV1 $imp
                    (GSLTPrattArgumentsConsV1
                      (GSLTPrattIdentifierApplicationV1
                        $y GSLTPrattArgumentsNilV1)
                      (GSLTPrattArgumentsConsV1
                        (GSLTPrattIdentifierApplicationV1
                          $x GSLTPrattArgumentsNilV1)
                        GSLTPrattArgumentsNilV1)))
                  GSLTPrattArgumentsNilV1))))))
       (if
         (==
           (collapse
             (gslt:mode:MM0RuntimeMathParseV1:1110
               (MM0SourceStateV1 $textual $notation) $scope
               (mm0:math:scalars:maximum-prefix)))
           ((gslt:result:MM0RuntimeMathParseV1:1110
              (GSLTPrattApplicationV1 $app
                (GSLTPrattArgumentsConsV1
                  (GSLTPrattIdentifierApplicationV1
                    $x GSLTPrattArgumentsNilV1)
                  GSLTPrattArgumentsNilV1)))))
         (if
           (==
             (collapse
               (gslt:mode:MM0RuntimeMathParseV1:1110
                 (MM0SourceStateV1 $textual $notation) $scope
                 (mm0:math:scalars:grouped)))
             ((gslt:result:MM0RuntimeMathParseV1:1110
                (GSLTPrattApplicationV1 $neg
                  (GSLTPrattArgumentsConsV1
                    (GSLTPrattApplicationV1 $imp
                      (GSLTPrattArgumentsConsV1
                        (GSLTPrattIdentifierApplicationV1
                          $x GSLTPrattArgumentsNilV1)
                        (GSLTPrattArgumentsConsV1
                          (GSLTPrattIdentifierApplicationV1
                            $y GSLTPrattArgumentsNilV1)
                          GSLTPrattArgumentsNilV1)))
                    GSLTPrattArgumentsNilV1)))))
           (if
             (==
               (collapse
                 (gslt:mode:MM0RuntimeMathParseV1:1110
                   (MM0SourceStateV1 $textual $notation) $scope
                   (mm0:math:scalars:incomplete)))
               ())
             (if
               (==
                 (collapse
                   (gslt:mode:MM0RuntimeMathParseV1:1110
                     (MM0SourceStateV1 $textual $notation) $scope
                     (mm0:math:scalars:plain-application)))
                 ((gslt:result:MM0RuntimeMathParseV1:1110
                    (GSLTPrattIdentifierApplicationV1 $imp
                      (GSLTPrattArgumentsConsV1
                        (GSLTPrattIdentifierApplicationV1
                          $x GSLTPrattArgumentsNilV1)
                        (GSLTPrattArgumentsConsV1
                          (GSLTPrattIdentifierApplicationV1
                            $y GSLTPrattArgumentsNilV1)
                          GSLTPrattArgumentsNilV1))))))
               (if
                 (==
                   (collapse
                     (gslt:mode:MM0RuntimeMathParseV1:1110
                       (MM0SourceStateV1 $textual $notation) $scope
                       (mm0:math:scalars:unknown-identifier)))
                   ())
                 MM0RuntimeMathParserAcceptedV1
                 MM0RuntimeMathParserUnexpectedUnknownIdentifierParseV1)
               MM0RuntimeMathParserUnexpectedPlainApplicationParseV1)
             MM0RuntimeMathParserUnexpectedIncompleteParseV1)
           MM0RuntimeMathParserUnexpectedGroupedParseV1)
         MM0RuntimeMathParserUnexpectedMaximumPrefixParseV1)
       MM0RuntimeMathParserUnexpectedRightAssociativeParseV1)))

; A second runtime source supplies a left-associative operator.  The generated
; parser is unchanged: only the live notation value differs.
!(let
   (gslt:result:MM0SourceEnvironmentFromAstV1:1000
     MM0AcceptedV1
     (MM0SourceStateV1 $textual $notation)
     $history)
   (let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
     (let
       (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
       (langdef:parse-file $mm0-langdef
         "tests/langdef/mm0/math_parser_left_associative.mm0")
       (gslt:fn:MM0SourceEnvironmentFromAstV1:1000 $ast)))
   (let*
     (($x (mm0:math:name:x))
      ($y (mm0:math:name:y))
      ($and (mm0:math:name:and))
      ($scope
        (MM0MathScopeConsV1 $x
          (MM0MathScopeConsV1 $y MM0MathScopeNilV1))))
     (if
       (==
         (collapse
           (gslt:mode:MM0RuntimeMathParseV1:1110
             (MM0SourceStateV1 $textual $notation) $scope
             (mm0:math:scalars:left-associative)))
         ((gslt:result:MM0RuntimeMathParseV1:1110
            (GSLTPrattApplicationV1 $and
              (GSLTPrattArgumentsConsV1
                (GSLTPrattApplicationV1 $and
                  (GSLTPrattArgumentsConsV1
                    (GSLTPrattIdentifierApplicationV1
                      $x GSLTPrattArgumentsNilV1)
                    (GSLTPrattArgumentsConsV1
                      (GSLTPrattIdentifierApplicationV1
                        $y GSLTPrattArgumentsNilV1)
                      GSLTPrattArgumentsNilV1)))
                (GSLTPrattArgumentsConsV1
                  (GSLTPrattIdentifierApplicationV1
                    $x GSLTPrattArgumentsNilV1)
                  GSLTPrattArgumentsNilV1))))))
       MM0RuntimeMathParserLeftAssociativeAcceptedV1
       MM0RuntimeMathParserUnexpectedLeftAssociativeParseV1)))
