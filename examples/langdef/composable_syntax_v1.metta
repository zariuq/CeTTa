; Run with --lang prime, --lang he --profile extended, or
; --lang petta --profile extended. Grammar values are data: compute operands
; with let before passing them to admission, inspection and composition.
!(import! &self langdef)

(= (syntax-demo:left)
   (langdef:syntax-gslt
     (GSLTLanguageDefWireV1 "Left"
       (LCons (TypeDecl "E" CarrierAst) LNil)
       (LCons (GrammarRule "X" "E" LNil
                (LCons (SyntaxTerminal "x") LNil) EvalNone)
         (LCons (GrammarRule "Pair" "E"
                  (LCons (TermSimple "a" (TBase "E"))
                    (LCons (TermSimple "b" (TBase "E")) LNil))
                  (LCons (SyntaxTerminal "(")
                    (LCons (SyntaxNonTerminal "a")
                      (LCons (SyntaxTerminal ",")
                        (LCons (SyntaxNonTerminal "b")
                          (LCons (SyntaxTerminal ")") LNil))))) EvalNone)
           LNil)) LNil LNil)
     (GSLTParserProfileLayerV1 "Left" "E" LNil LNil)
     (SyntaxLexicalPolicyV1 ScalarScannerlessAllV1)
     (SyntaxOriginsV1
       (LCons (SyntaxOriginV1 "shared-categories" 0) LNil)
       (LCons (SyntaxOriginV1 "Left" 0)
         (LCons (SyntaxOriginV1 "Left" 1) LNil)) LNil LNil)))

(= (syntax-demo:right)
   (langdef:syntax-gslt
     (GSLTLanguageDefWireV1 "Right"
       (LCons (TypeDecl "E" CarrierAst) LNil)
       (LCons (GrammarRule "Y" "E" LNil
                (LCons (SyntaxTerminal "y") LNil) EvalNone) LNil) LNil LNil)
     (GSLTParserProfileLayerV1 "Right" "E" LNil LNil)
     (SyntaxLexicalPolicyV1 ScalarScannerlessAllV1)
     (SyntaxOriginsV1
       (LCons (SyntaxOriginV1 "shared-categories" 0) LNil)
       (LCons (SyntaxOriginV1 "Right" 0) LNil) LNil LNil)))

; Neither operand alone parses (x,y). The joined value preserves their
; signatures and declaration origins. Evidence parsing checks GLL against
; GLR and returns the canonical packed forest, not only its digest.
!(let $left (syntax-demo:left)
   (let $right (syntax-demo:right)
     (let $joined (langdef:join-syntax-gslts "Combined" "E" $left $right)
       (let $inspection (langdef:inspect-syntax-gslt $joined)
         (let $parser (langdef:load-syntax-gslt-parser $joined)
           (let $evidence (langdef:parse-authored-text-evidence $parser "(x,y)" 2000)
             (let $closed (langdef:close-authored-parser $parser)
               (SyntaxExampleV1 $inspection $evidence $closed))))))))
