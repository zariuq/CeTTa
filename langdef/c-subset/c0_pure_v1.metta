(GSLTLanguageDefWireV1
  "C0Pure"
  (LCons (TypeDecl "Integer" CarrierBuiltinInt)
    (LCons (TypeDecl "String" CarrierBuiltinString)
      (LCons (TypeDecl "Nat" CarrierAst)
        (LCons (TypeDecl "SlotId" CarrierAst)
          (LCons (TypeDecl "Label" CarrierAst)
            (LCons (TypeDecl "ExternalId" CarrierAst)
              (LCons (TypeDecl "Value" CarrierAst)
                (LCons (TypeDecl "Slot" CarrierAst)
                  (LCons (TypeDecl "Store" CarrierAst)
                    (LCons (TypeDecl "Instruction" CarrierAst)
                      (LCons (TypeDecl "InstructionList" CarrierAst)
                        (LCons (TypeDecl "ExternalDecl" CarrierAst)
                          (LCons (TypeDecl "ExternalList" CarrierAst)
                            (LCons (TypeDecl "Program" CarrierAst)
                              (LCons (TypeDecl "Fuel" CarrierAst)
                                (LCons (TypeDecl "Fault" CarrierAst)
                                  (LCons (TypeDecl "ExternalOutcome" CarrierAst)
                                    (LCons (TypeDecl "Outcome" CarrierAst)
                                      (LCons (TypeDecl "Event" CarrierAst)
                                        (LCons (TypeDecl "Receipt" CarrierAst)
                                          (LCons (TypeDecl "Config" CarrierAst)
                                            LNil)))))))))))))))))))))
  (LCons
    (GrammarRule "c0:nat-zero" "Nat" LNil
      (LCons (SyntaxTerminal "C0NatZero") LNil) EvalNone)
    (LCons
      (GrammarRule "c0:nat-succ" "Nat"
        (LCons (TermSimple "prior" (TBase "Nat")) LNil)
        (LCons (SyntaxTerminal "C0NatSucc") LNil) EvalNone)
      (LCons
        (GrammarRule "c0:slot-id" "SlotId"
          (LCons (TermSimple "index" (TBase "Nat")) LNil)
          (LCons (SyntaxTerminal "C0SlotId") LNil) EvalNone)
        (LCons
          (GrammarRule "c0:label" "Label"
            (LCons (TermSimple "index" (TBase "Nat")) LNil)
            (LCons (SyntaxTerminal "C0Label") LNil) EvalNone)
          (LCons
            (GrammarRule "c0:external-id" "ExternalId"
              (LCons (TermSimple "index" (TBase "Nat")) LNil)
              (LCons (SyntaxTerminal "C0ExternalId") LNil) EvalNone)
            (LCons
              (GrammarRule "c0:exact-integer" "Value"
                (LCons (TermSimple "value" (TBase "Integer")) LNil)
                (LCons (SyntaxTerminal "C0ExactInteger") LNil) EvalNone)
              (LCons
                (GrammarRule "c0:slot-empty" "Slot" LNil
                  (LCons (SyntaxTerminal "C0SlotEmpty") LNil) EvalNone)
                (LCons
                  (GrammarRule "c0:slot-value" "Slot"
                    (LCons (TermSimple "value" (TBase "Value")) LNil)
                    (LCons (SyntaxTerminal "C0SlotValue") LNil) EvalNone)
                  (LCons
                    (GrammarRule "c0:store-nil" "Store" LNil
                      (LCons (SyntaxTerminal "C0StoreNil") LNil) EvalNone)
                    (LCons
                      (GrammarRule "c0:store-cons" "Store"
                        (LCons (TermSimple "slot" (TBase "Slot"))
                          (LCons (TermSimple "rest" (TBase "Store")) LNil))
                        (LCons (SyntaxTerminal "C0StoreCons") LNil) EvalNone)
                      (LCons
                        (GrammarRule "c0:branch-zero" "Instruction"
                          (LCons (TermSimple "slot" (TBase "SlotId"))
                            (LCons (TermSimple "ifZero" (TBase "Label"))
                              (LCons (TermSimple "ifNonzero" (TBase "Label")) LNil)))
                          (LCons (SyntaxTerminal "C0BranchZero") LNil) EvalNone)
                        (LCons
                          (GrammarRule "c0:call-binary" "Instruction"
                            (LCons (TermSimple "external" (TBase "ExternalId"))
                              (LCons (TermSimple "ifValue" (TBase "Label"))
                                (LCons (TermSimple "ifLanguageFault" (TBase "Label"))
                                  (LCons (TermSimple "ifEngineFault" (TBase "Label"))
                                    (LCons (TermSimple "ifResourceFault" (TBase "Label")) LNil)))))
                            (LCons (SyntaxTerminal "C0CallBinary") LNil) EvalNone)
                          (LCons
                            (GrammarRule "c0:return-value" "Instruction"
                              (LCons (TermSimple "slot" (TBase "SlotId")) LNil)
                              (LCons (SyntaxTerminal "C0ReturnValue") LNil) EvalNone)
                            (LCons
                              (GrammarRule "c0:return-declined" "Instruction" LNil
                                (LCons (SyntaxTerminal "C0ReturnDeclined") LNil) EvalNone)
                              (LCons
                                (GrammarRule "c0:return-language-fault" "Instruction"
                                  (LCons (TermSimple "fault" (TBase "Fault")) LNil)
                                  (LCons (SyntaxTerminal "C0ReturnLanguageFault") LNil) EvalNone)
                                (LCons
                                  (GrammarRule "c0:return-engine-fault" "Instruction"
                                    (LCons (TermSimple "fault" (TBase "Fault")) LNil)
                                    (LCons (SyntaxTerminal "C0ReturnEngineFault") LNil) EvalNone)
                                  (LCons
                                    (GrammarRule "c0:return-resource-fault" "Instruction"
                                      (LCons (TermSimple "fault" (TBase "Fault")) LNil)
                                      (LCons (SyntaxTerminal "C0ReturnResourceFault") LNil) EvalNone)
                                    (LCons
                                      (GrammarRule "c0:instruction-nil" "InstructionList" LNil
                                        (LCons (SyntaxTerminal "C0InstructionNil") LNil) EvalNone)
                                      (LCons
                                        (GrammarRule "c0:instruction-cons" "InstructionList"
                                          (LCons (TermSimple "instruction" (TBase "Instruction"))
                                            (LCons (TermSimple "rest" (TBase "InstructionList")) LNil))
                                          (LCons (SyntaxTerminal "C0InstructionCons") LNil) EvalNone)
                                        (LCons
                                          (GrammarRule "c0:binary-external" "ExternalDecl"
                                            (LCons (TermSimple "external" (TBase "ExternalId"))
                                              (LCons (TermSimple "linkName" (TBase "String"))
                                                (LCons (TermSimple "firstInput" (TBase "SlotId"))
                                                  (LCons (TermSimple "secondInput" (TBase "SlotId"))
                                                    (LCons (TermSimple "output" (TBase "SlotId")) LNil)))))
                                            (LCons (SyntaxTerminal "C0BinaryExternal") LNil) EvalNone)
                                          (LCons
                                            (GrammarRule "c0:external-nil" "ExternalList" LNil
                                              (LCons (SyntaxTerminal "C0ExternalNil") LNil) EvalNone)
                                            (LCons
                                              (GrammarRule "c0:external-cons" "ExternalList"
                                                (LCons (TermSimple "external" (TBase "ExternalDecl"))
                                                  (LCons (TermSimple "rest" (TBase "ExternalList")) LNil))
                                                (LCons (SyntaxTerminal "C0ExternalCons") LNil) EvalNone)
                                              (LCons
                                                (GrammarRule "c0:program" "Program"
                                                  (LCons (TermSimple "instructions" (TBase "InstructionList"))
                                                    (LCons (TermSimple "externals" (TBase "ExternalList"))
                                                      (LCons (TermSimple "entry" (TBase "Label")) LNil)))
                                                  (LCons (SyntaxTerminal "C0Program") LNil) EvalNone)
                                                (LCons
                                                  (GrammarRule "c0:fuel-infinite" "Fuel" LNil
                                                    (LCons (SyntaxTerminal "C0FuelInfinite") LNil) EvalNone)
                                                  (LCons
                                                    (GrammarRule "c0:fuel-zero" "Fuel" LNil
                                                      (LCons (SyntaxTerminal "C0FuelZero") LNil) EvalNone)
                                                    (LCons
                                                      (GrammarRule "c0:fuel-succ" "Fuel"
                                                        (LCons (TermSimple "prior" (TBase "Fuel")) LNil)
                                                        (LCons (SyntaxTerminal "C0FuelSucc") LNil) EvalNone)
                                                      (LCons
                                                        (GrammarRule "c0:fault" "Fault"
                                                          (LCons (TermSimple "name" (TBase "String")) LNil)
                                                          (LCons (SyntaxTerminal "C0Fault") LNil) EvalNone)
                                                        (LCons
                                                          (GrammarRule "c0:external-value" "ExternalOutcome"
                                                            (LCons (TermSimple "store" (TBase "Store")) LNil)
                                                            (LCons (SyntaxTerminal "C0ExternalValue") LNil) EvalNone)
                                                          (LCons
                                                            (GrammarRule "c0:external-language-fault" "ExternalOutcome"
                                                              (LCons (TermSimple "fault" (TBase "Fault")) LNil)
                                                              (LCons (SyntaxTerminal "C0ExternalLanguageFault") LNil) EvalNone)
                                                            (LCons
                                                              (GrammarRule "c0:external-engine-fault" "ExternalOutcome"
                                                                (LCons (TermSimple "fault" (TBase "Fault")) LNil)
                                                                (LCons (SyntaxTerminal "C0ExternalEngineFault") LNil) EvalNone)
                                                              (LCons
                                                                (GrammarRule "c0:external-resource-fault" "ExternalOutcome"
                                                                  (LCons (TermSimple "fault" (TBase "Fault")) LNil)
                                                                  (LCons (SyntaxTerminal "C0ExternalResourceFault") LNil) EvalNone)
                                                                (LCons
                                                                  (GrammarRule "c0:outcome-value" "Outcome"
                                                                    (LCons (TermSimple "value" (TBase "Value")) LNil)
                                                                    (LCons (SyntaxTerminal "C0Value") LNil) EvalNone)
                                                                  (LCons
                                                                    (GrammarRule "c0:outcome-declined" "Outcome" LNil
                                                                      (LCons (SyntaxTerminal "C0Declined") LNil) EvalNone)
                                                                    (LCons
                                                                      (GrammarRule "c0:outcome-language-fault" "Outcome"
                                                                        (LCons (TermSimple "fault" (TBase "Fault")) LNil)
                                                                        (LCons (SyntaxTerminal "C0LanguageFault") LNil) EvalNone)
                                                                      (LCons
                                                                        (GrammarRule "c0:outcome-engine-fault" "Outcome"
                                                                          (LCons (TermSimple "fault" (TBase "Fault")) LNil)
                                                                          (LCons (SyntaxTerminal "C0EngineFault") LNil) EvalNone)
                                                                        (LCons
                                                                          (GrammarRule "c0:outcome-resource-fault" "Outcome"
                                                                            (LCons (TermSimple "fault" (TBase "Fault")) LNil)
                                                                            (LCons (SyntaxTerminal "C0ResourceFault") LNil) EvalNone)
                                                                          (LCons
                                                                            (GrammarRule "c0:step-event" "Event"
                                                                              (LCons (TermSimple "at" (TBase "Label")) LNil)
                                                                              (LCons (SyntaxTerminal "C0StepEvent") LNil) EvalNone)
                                                                            (LCons
                                                                              (GrammarRule "c0:external-event" "Event"
                                                                                (LCons (TermSimple "external" (TBase "ExternalId"))
                                                                                  (LCons (TermSimple "outcome" (TBase "ExternalOutcome")) LNil))
                                                                                (LCons (SyntaxTerminal "C0ExternalEvent") LNil) EvalNone)
                                                                              (LCons
                                                                                (GrammarRule "c0:fuel-exhausted-event" "Event"
                                                                                  (LCons (TermSimple "at" (TBase "Label")) LNil)
                                                                                  (LCons (SyntaxTerminal "C0FuelExhaustedEvent") LNil) EvalNone)
                                                                                (LCons
                                                                                  (GrammarRule "c0:receipt-nil" "Receipt" LNil
                                                                                    (LCons (SyntaxTerminal "C0ReceiptNil") LNil) EvalNone)
                                                                                  (LCons
                                                                                    (GrammarRule "c0:receipt-cons" "Receipt"
                                                                                      (LCons (TermSimple "event" (TBase "Event"))
                                                                                        (LCons (TermSimple "prior" (TBase "Receipt")) LNil))
                                                                                      (LCons (SyntaxTerminal "C0ReceiptCons") LNil) EvalNone)
                                                                                    (LCons
                                                                                      (GrammarRule "c0:run" "Config"
                                                                                        (LCons (TermSimple "program" (TBase "Program"))
                                                                                          (LCons (TermSimple "pc" (TBase "Label"))
                                                                                            (LCons (TermSimple "store" (TBase "Store"))
                                                                                              (LCons (TermSimple "fuel" (TBase "Fuel"))
                                                                                                (LCons (TermSimple "receipt" (TBase "Receipt")) LNil)))))
                                                                                        (LCons (SyntaxTerminal "C0Run") LNil)
                                                                                        (EvalSome EvalRewrite))
                                                                                      (LCons
                                                                                        (GrammarRule "c0:halted" "Config"
                                                                                          (LCons (TermSimple "outcome" (TBase "Outcome"))
                                                                                            (LCons (TermSimple "receipt" (TBase "Receipt")) LNil))
                                                                                          (LCons (SyntaxTerminal "C0Halted") LNil) EvalNone)
                                                                                        LNil)))))))))))))))))))))))))))))))))))))))))))
  LNil
  (LCons
    (RewriteRule "c0:fuel-exhausted"
      (LCons (TypeBinding "program" (TBase "Program"))
        (LCons (TypeBinding "pc" (TBase "Label"))
          (LCons (TypeBinding "store" (TBase "Store"))
            (LCons (TypeBinding "receipt" (TBase "Receipt"))
              (LCons (TypeBinding "fault" (TBase "Fault")) LNil)))))
      (LCons
        (RelationQuery "C0StepLimitFault"
          (LCons (FVar "fault") LNil))
        LNil)
      (PApp "c0:run"
        (LCons (FVar "program")
          (LCons (FVar "pc")
            (LCons (FVar "store")
              (LCons (PApp "c0:fuel-zero" LNil)
                (LCons (FVar "receipt") LNil))))))
      (PApp "c0:halted"
        (LCons (PApp "c0:outcome-resource-fault" (LCons (FVar "fault") LNil))
          (LCons
            (PApp "c0:receipt-cons"
              (LCons (PApp "c0:fuel-exhausted-event" (LCons (FVar "pc") LNil))
                (LCons (FVar "receipt") LNil)))
            LNil))))
    (LCons
      (RewriteRule "c0:branch-zero"
        (LCons (TypeBinding "program" (TBase "Program"))
          (LCons (TypeBinding "pc" (TBase "Label"))
            (LCons (TypeBinding "store" (TBase "Store"))
              (LCons (TypeBinding "fuel" (TBase "Fuel"))
                (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                  (LCons (TypeBinding "receipt" (TBase "Receipt"))
                    (LCons (TypeBinding "slot" (TBase "SlotId"))
                      (LCons (TypeBinding "value" (TBase "Value"))
                        (LCons (TypeBinding "ifZero" (TBase "Label"))
                          (LCons (TypeBinding "ifNonzero" (TBase "Label")) LNil))))))))))
        (LCons (RelationQuery "C0ConsumeFuel"
            (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
          (LCons (RelationQuery "C0FetchInstruction"
              (LCons (FVar "program")
                (LCons (FVar "pc")
                  (LCons
                    (PApp "c0:branch-zero"
                      (LCons (FVar "slot")
                        (LCons (FVar "ifZero")
                          (LCons (FVar "ifNonzero") LNil))))
                    LNil))))
            (LCons (RelationQuery "C0ReadSlot"
                (LCons (FVar "store")
                  (LCons (FVar "slot") (LCons (FVar "value") LNil))))
              (LCons (RelationQuery "C0IsZero"
                  (LCons (FVar "value") LNil))
                LNil))))
        (PApp "c0:run"
          (LCons (FVar "program")
            (LCons (FVar "pc")
              (LCons (FVar "store")
                (LCons (FVar "fuel") (LCons (FVar "receipt") LNil))))))
        (PApp "c0:run"
          (LCons (FVar "program")
            (LCons (FVar "ifZero")
              (LCons (FVar "store")
                (LCons (FVar "nextFuel")
                  (LCons
                    (PApp "c0:receipt-cons"
                      (LCons (PApp "c0:step-event" (LCons (FVar "pc") LNil))
                        (LCons (FVar "receipt") LNil)))
                    LNil)))))))
      (LCons
        (RewriteRule "c0:branch-nonzero"
          (LCons (TypeBinding "program" (TBase "Program"))
            (LCons (TypeBinding "pc" (TBase "Label"))
              (LCons (TypeBinding "store" (TBase "Store"))
                (LCons (TypeBinding "fuel" (TBase "Fuel"))
                  (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                    (LCons (TypeBinding "receipt" (TBase "Receipt"))
                      (LCons (TypeBinding "slot" (TBase "SlotId"))
                        (LCons (TypeBinding "value" (TBase "Value"))
                          (LCons (TypeBinding "ifZero" (TBase "Label"))
                            (LCons (TypeBinding "ifNonzero" (TBase "Label")) LNil))))))))))
          (LCons (RelationQuery "C0ConsumeFuel"
              (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
            (LCons (RelationQuery "C0FetchInstruction"
                (LCons (FVar "program")
                  (LCons (FVar "pc")
                    (LCons
                      (PApp "c0:branch-zero"
                        (LCons (FVar "slot")
                          (LCons (FVar "ifZero")
                            (LCons (FVar "ifNonzero") LNil))))
                      LNil))))
              (LCons (RelationQuery "C0ReadSlot"
                  (LCons (FVar "store")
                    (LCons (FVar "slot") (LCons (FVar "value") LNil))))
                (LCons (RelationQuery "C0IsNonzero"
                    (LCons (FVar "value") LNil))
                  LNil))))
          (PApp "c0:run"
            (LCons (FVar "program")
              (LCons (FVar "pc")
                (LCons (FVar "store")
                  (LCons (FVar "fuel") (LCons (FVar "receipt") LNil))))))
          (PApp "c0:run"
            (LCons (FVar "program")
              (LCons (FVar "ifNonzero")
                (LCons (FVar "store")
                  (LCons (FVar "nextFuel")
                    (LCons
                      (PApp "c0:receipt-cons"
                        (LCons (PApp "c0:step-event" (LCons (FVar "pc") LNil))
                          (LCons (FVar "receipt") LNil)))
                      LNil)))))))
        (LCons
          (RewriteRule "c0:call-value"
            (LCons (TypeBinding "program" (TBase "Program"))
              (LCons (TypeBinding "pc" (TBase "Label"))
                (LCons (TypeBinding "store" (TBase "Store"))
                  (LCons (TypeBinding "nextStore" (TBase "Store"))
                    (LCons (TypeBinding "fuel" (TBase "Fuel"))
                      (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                        (LCons (TypeBinding "receipt" (TBase "Receipt"))
                          (LCons (TypeBinding "external" (TBase "ExternalId"))
                            (LCons (TypeBinding "ifValue" (TBase "Label"))
                              (LCons (TypeBinding "ifLanguageFault" (TBase "Label"))
                                (LCons (TypeBinding "ifEngineFault" (TBase "Label"))
                                  (LCons (TypeBinding "ifResourceFault" (TBase "Label")) LNil))))))))))))
            (LCons (RelationQuery "C0ConsumeFuel"
                (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
              (LCons (RelationQuery "C0FetchInstruction"
                  (LCons (FVar "program")
                    (LCons (FVar "pc")
                      (LCons (PApp "c0:call-binary"
                          (LCons (FVar "external")
                            (LCons (FVar "ifValue")
                              (LCons (FVar "ifLanguageFault")
                                (LCons (FVar "ifEngineFault")
                                  (LCons (FVar "ifResourceFault") LNil))))))
                        LNil))))
                (LCons (RelationQuery "C0CallBinaryExternal"
                    (LCons (FVar "program")
                      (LCons (FVar "external")
                        (LCons (FVar "store")
                          (LCons (PApp "c0:external-value"
                              (LCons (FVar "nextStore") LNil))
                            LNil)))))
                  LNil)))
            (PApp "c0:run"
              (LCons (FVar "program")
                (LCons (FVar "pc")
                  (LCons (FVar "store")
                    (LCons (FVar "fuel") (LCons (FVar "receipt") LNil))))))
            (PApp "c0:run"
              (LCons (FVar "program")
                (LCons (FVar "ifValue")
                  (LCons (FVar "nextStore")
                    (LCons (FVar "nextFuel")
                      (LCons (PApp "c0:receipt-cons"
                          (LCons (PApp "c0:external-event"
                              (LCons (FVar "external")
                                (LCons (PApp "c0:external-value"
                                    (LCons (FVar "nextStore") LNil))
                                  LNil)))
                            (LCons (PApp "c0:receipt-cons"
                                (LCons (PApp "c0:step-event"
                                    (LCons (FVar "pc") LNil))
                                  (LCons (FVar "receipt") LNil)))
                              LNil)))
                        LNil)))))))
          (LCons
            (RewriteRule "c0:call-language-fault"
              (LCons (TypeBinding "program" (TBase "Program"))
                (LCons (TypeBinding "pc" (TBase "Label"))
                  (LCons (TypeBinding "store" (TBase "Store"))
                    (LCons (TypeBinding "fuel" (TBase "Fuel"))
                      (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                        (LCons (TypeBinding "receipt" (TBase "Receipt"))
                          (LCons (TypeBinding "external" (TBase "ExternalId"))
                            (LCons (TypeBinding "fault" (TBase "Fault"))
                              (LCons (TypeBinding "ifValue" (TBase "Label"))
                                (LCons (TypeBinding "ifLanguageFault" (TBase "Label"))
                                  (LCons (TypeBinding "ifEngineFault" (TBase "Label"))
                                    (LCons (TypeBinding "ifResourceFault" (TBase "Label")) LNil))))))))))))
              (LCons (RelationQuery "C0ConsumeFuel"
                  (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
                (LCons (RelationQuery "C0FetchInstruction"
                    (LCons (FVar "program")
                      (LCons (FVar "pc")
                        (LCons (PApp "c0:call-binary"
                            (LCons (FVar "external")
                              (LCons (FVar "ifValue")
                                (LCons (FVar "ifLanguageFault")
                                  (LCons (FVar "ifEngineFault")
                                    (LCons (FVar "ifResourceFault") LNil))))))
                          LNil))))
                  (LCons (RelationQuery "C0CallBinaryExternal"
                      (LCons (FVar "program")
                        (LCons (FVar "external")
                          (LCons (FVar "store")
                            (LCons (PApp "c0:external-language-fault"
                                (LCons (FVar "fault") LNil))
                              LNil)))))
                    LNil)))
              (PApp "c0:run"
                (LCons (FVar "program")
                  (LCons (FVar "pc")
                    (LCons (FVar "store")
                      (LCons (FVar "fuel") (LCons (FVar "receipt") LNil))))))
              (PApp "c0:run"
                (LCons (FVar "program")
                  (LCons (FVar "ifLanguageFault")
                    (LCons (FVar "store")
                      (LCons (FVar "nextFuel")
                        (LCons (PApp "c0:receipt-cons"
                            (LCons (PApp "c0:external-event"
                                (LCons (FVar "external")
                                  (LCons (PApp "c0:external-language-fault"
                                      (LCons (FVar "fault") LNil))
                                    LNil)))
                              (LCons (PApp "c0:receipt-cons"
                                  (LCons (PApp "c0:step-event"
                                      (LCons (FVar "pc") LNil))
                                    (LCons (FVar "receipt") LNil)))
                                LNil)))
                          LNil)))))))
            (LCons
              (RewriteRule "c0:call-engine-fault"
                (LCons (TypeBinding "program" (TBase "Program"))
                  (LCons (TypeBinding "pc" (TBase "Label"))
                    (LCons (TypeBinding "store" (TBase "Store"))
                      (LCons (TypeBinding "fuel" (TBase "Fuel"))
                        (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                          (LCons (TypeBinding "receipt" (TBase "Receipt"))
                            (LCons (TypeBinding "external" (TBase "ExternalId"))
                              (LCons (TypeBinding "fault" (TBase "Fault"))
                                (LCons (TypeBinding "ifValue" (TBase "Label"))
                                  (LCons (TypeBinding "ifLanguageFault" (TBase "Label"))
                                    (LCons (TypeBinding "ifEngineFault" (TBase "Label"))
                                      (LCons (TypeBinding "ifResourceFault" (TBase "Label")) LNil))))))))))))
                (LCons (RelationQuery "C0ConsumeFuel"
                    (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
                  (LCons (RelationQuery "C0FetchInstruction"
                      (LCons (FVar "program")
                        (LCons (FVar "pc")
                          (LCons (PApp "c0:call-binary"
                              (LCons (FVar "external")
                                (LCons (FVar "ifValue")
                                  (LCons (FVar "ifLanguageFault")
                                    (LCons (FVar "ifEngineFault")
                                      (LCons (FVar "ifResourceFault") LNil))))))
                            LNil))))
                    (LCons (RelationQuery "C0CallBinaryExternal"
                        (LCons (FVar "program")
                          (LCons (FVar "external")
                            (LCons (FVar "store")
                              (LCons (PApp "c0:external-engine-fault"
                                  (LCons (FVar "fault") LNil))
                                LNil)))))
                      LNil)))
                (PApp "c0:run"
                  (LCons (FVar "program")
                    (LCons (FVar "pc")
                      (LCons (FVar "store")
                        (LCons (FVar "fuel") (LCons (FVar "receipt") LNil))))))
                (PApp "c0:run"
                  (LCons (FVar "program")
                    (LCons (FVar "ifEngineFault")
                      (LCons (FVar "store")
                        (LCons (FVar "nextFuel")
                          (LCons (PApp "c0:receipt-cons"
                              (LCons (PApp "c0:external-event"
                                  (LCons (FVar "external")
                                    (LCons (PApp "c0:external-engine-fault"
                                        (LCons (FVar "fault") LNil))
                                      LNil)))
                                (LCons (PApp "c0:receipt-cons"
                                    (LCons (PApp "c0:step-event"
                                        (LCons (FVar "pc") LNil))
                                      (LCons (FVar "receipt") LNil)))
                                  LNil)))
                            LNil)))))))
              (LCons
                (RewriteRule "c0:call-resource-fault"
                  (LCons (TypeBinding "program" (TBase "Program"))
                    (LCons (TypeBinding "pc" (TBase "Label"))
                      (LCons (TypeBinding "store" (TBase "Store"))
                        (LCons (TypeBinding "fuel" (TBase "Fuel"))
                          (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                            (LCons (TypeBinding "receipt" (TBase "Receipt"))
                              (LCons (TypeBinding "external" (TBase "ExternalId"))
                                (LCons (TypeBinding "fault" (TBase "Fault"))
                                  (LCons (TypeBinding "ifValue" (TBase "Label"))
                                    (LCons (TypeBinding "ifLanguageFault" (TBase "Label"))
                                      (LCons (TypeBinding "ifEngineFault" (TBase "Label"))
                                        (LCons (TypeBinding "ifResourceFault" (TBase "Label")) LNil))))))))))))
                  (LCons (RelationQuery "C0ConsumeFuel"
                      (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
                    (LCons (RelationQuery "C0FetchInstruction"
                        (LCons (FVar "program")
                          (LCons (FVar "pc")
                            (LCons (PApp "c0:call-binary"
                                (LCons (FVar "external")
                                  (LCons (FVar "ifValue")
                                    (LCons (FVar "ifLanguageFault")
                                      (LCons (FVar "ifEngineFault")
                                        (LCons (FVar "ifResourceFault") LNil))))))
                              LNil))))
                      (LCons (RelationQuery "C0CallBinaryExternal"
                          (LCons (FVar "program")
                            (LCons (FVar "external")
                              (LCons (FVar "store")
                                (LCons (PApp "c0:external-resource-fault"
                                    (LCons (FVar "fault") LNil))
                                  LNil)))))
                        LNil)))
                  (PApp "c0:run"
                    (LCons (FVar "program")
                      (LCons (FVar "pc")
                        (LCons (FVar "store")
                          (LCons (FVar "fuel") (LCons (FVar "receipt") LNil))))))
                  (PApp "c0:run"
                    (LCons (FVar "program")
                      (LCons (FVar "ifResourceFault")
                        (LCons (FVar "store")
                          (LCons (FVar "nextFuel")
                            (LCons (PApp "c0:receipt-cons"
                                (LCons (PApp "c0:external-event"
                                    (LCons (FVar "external")
                                      (LCons (PApp "c0:external-resource-fault"
                                          (LCons (FVar "fault") LNil))
                                        LNil)))
                                  (LCons (PApp "c0:receipt-cons"
                                      (LCons (PApp "c0:step-event"
                                          (LCons (FVar "pc") LNil))
                                        (LCons (FVar "receipt") LNil)))
                                    LNil)))
                              LNil)))))))
                (LCons
                  (RewriteRule "c0:return-value"
                    (LCons (TypeBinding "program" (TBase "Program"))
                      (LCons (TypeBinding "pc" (TBase "Label"))
                        (LCons (TypeBinding "store" (TBase "Store"))
                          (LCons (TypeBinding "fuel" (TBase "Fuel"))
                            (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                              (LCons (TypeBinding "receipt" (TBase "Receipt"))
                                (LCons (TypeBinding "slot" (TBase "SlotId"))
                                  (LCons (TypeBinding "value" (TBase "Value")) LNil))))))))
                    (LCons (RelationQuery "C0ConsumeFuel"
                        (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
                      (LCons (RelationQuery "C0FetchInstruction"
                          (LCons (FVar "program")
                            (LCons (FVar "pc")
                              (LCons (PApp "c0:return-value"
                                  (LCons (FVar "slot") LNil))
                                LNil))))
                        (LCons (RelationQuery "C0ReadSlot"
                            (LCons (FVar "store")
                              (LCons (FVar "slot")
                                (LCons (FVar "value") LNil))))
                          LNil)))
                    (PApp "c0:run"
                      (LCons (FVar "program")
                        (LCons (FVar "pc")
                          (LCons (FVar "store")
                            (LCons (FVar "fuel")
                              (LCons (FVar "receipt") LNil))))))
                    (PApp "c0:halted"
                      (LCons (PApp "c0:outcome-value" (LCons (FVar "value") LNil))
                        (LCons (PApp "c0:receipt-cons"
                            (LCons (PApp "c0:step-event"
                                (LCons (FVar "pc") LNil))
                              (LCons (FVar "receipt") LNil)))
                          LNil))))
                  (LCons
                    (RewriteRule "c0:return-declined"
                      (LCons (TypeBinding "program" (TBase "Program"))
                        (LCons (TypeBinding "pc" (TBase "Label"))
                          (LCons (TypeBinding "store" (TBase "Store"))
                            (LCons (TypeBinding "fuel" (TBase "Fuel"))
                              (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                                (LCons (TypeBinding "receipt" (TBase "Receipt")) LNil))))))
                      (LCons (RelationQuery "C0ConsumeFuel"
                          (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
                        (LCons (RelationQuery "C0FetchInstruction"
                            (LCons (FVar "program")
                              (LCons (FVar "pc")
                                (LCons (PApp "c0:return-declined" LNil) LNil))))
                          LNil))
                      (PApp "c0:run"
                        (LCons (FVar "program")
                          (LCons (FVar "pc")
                            (LCons (FVar "store")
                              (LCons (FVar "fuel")
                                (LCons (FVar "receipt") LNil))))))
                      (PApp "c0:halted"
                        (LCons (PApp "c0:outcome-declined" LNil)
                          (LCons (PApp "c0:receipt-cons"
                              (LCons (PApp "c0:step-event"
                                  (LCons (FVar "pc") LNil))
                                (LCons (FVar "receipt") LNil)))
                            LNil))))
                    (LCons
                      (RewriteRule "c0:return-language-fault"
                        (LCons (TypeBinding "program" (TBase "Program"))
                          (LCons (TypeBinding "pc" (TBase "Label"))
                            (LCons (TypeBinding "store" (TBase "Store"))
                              (LCons (TypeBinding "fuel" (TBase "Fuel"))
                                (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                                  (LCons (TypeBinding "receipt" (TBase "Receipt"))
                                    (LCons (TypeBinding "fault" (TBase "Fault")) LNil)))))))
                        (LCons (RelationQuery "C0ConsumeFuel"
                            (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
                          (LCons (RelationQuery "C0FetchInstruction"
                              (LCons (FVar "program")
                                (LCons (FVar "pc")
                                  (LCons (PApp "c0:return-language-fault"
                                      (LCons (FVar "fault") LNil))
                                    LNil))))
                            LNil))
                        (PApp "c0:run"
                          (LCons (FVar "program")
                            (LCons (FVar "pc")
                              (LCons (FVar "store")
                                (LCons (FVar "fuel")
                                  (LCons (FVar "receipt") LNil))))))
                        (PApp "c0:halted"
                          (LCons (PApp "c0:outcome-language-fault"
                              (LCons (FVar "fault") LNil))
                            (LCons (PApp "c0:receipt-cons"
                                (LCons (PApp "c0:step-event"
                                    (LCons (FVar "pc") LNil))
                                  (LCons (FVar "receipt") LNil)))
                              LNil))))
                      (LCons
                        (RewriteRule "c0:return-engine-fault"
                          (LCons (TypeBinding "program" (TBase "Program"))
                            (LCons (TypeBinding "pc" (TBase "Label"))
                              (LCons (TypeBinding "store" (TBase "Store"))
                                (LCons (TypeBinding "fuel" (TBase "Fuel"))
                                  (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                                    (LCons (TypeBinding "receipt" (TBase "Receipt"))
                                      (LCons (TypeBinding "fault" (TBase "Fault")) LNil)))))))
                          (LCons (RelationQuery "C0ConsumeFuel"
                              (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
                            (LCons (RelationQuery "C0FetchInstruction"
                                (LCons (FVar "program")
                                  (LCons (FVar "pc")
                                    (LCons (PApp "c0:return-engine-fault"
                                        (LCons (FVar "fault") LNil))
                                      LNil))))
                              LNil))
                          (PApp "c0:run"
                            (LCons (FVar "program")
                              (LCons (FVar "pc")
                                (LCons (FVar "store")
                                  (LCons (FVar "fuel")
                                    (LCons (FVar "receipt") LNil))))))
                          (PApp "c0:halted"
                            (LCons (PApp "c0:outcome-engine-fault"
                                (LCons (FVar "fault") LNil))
                              (LCons (PApp "c0:receipt-cons"
                                  (LCons (PApp "c0:step-event"
                                      (LCons (FVar "pc") LNil))
                                    (LCons (FVar "receipt") LNil)))
                                LNil))))
                        (LCons
                          (RewriteRule "c0:return-resource-fault"
                            (LCons (TypeBinding "program" (TBase "Program"))
                              (LCons (TypeBinding "pc" (TBase "Label"))
                                (LCons (TypeBinding "store" (TBase "Store"))
                                  (LCons (TypeBinding "fuel" (TBase "Fuel"))
                                    (LCons (TypeBinding "nextFuel" (TBase "Fuel"))
                                      (LCons (TypeBinding "receipt" (TBase "Receipt"))
                                        (LCons (TypeBinding "fault" (TBase "Fault")) LNil)))))))
                            (LCons (RelationQuery "C0ConsumeFuel"
                                (LCons (FVar "fuel") (LCons (FVar "nextFuel") LNil)))
                              (LCons (RelationQuery "C0FetchInstruction"
                                  (LCons (FVar "program")
                                    (LCons (FVar "pc")
                                      (LCons (PApp "c0:return-resource-fault"
                                          (LCons (FVar "fault") LNil))
                                        LNil))))
                                LNil))
                            (PApp "c0:run"
                              (LCons (FVar "program")
                                (LCons (FVar "pc")
                                  (LCons (FVar "store")
                                    (LCons (FVar "fuel")
                                      (LCons (FVar "receipt") LNil))))))
                            (PApp "c0:halted"
                              (LCons (PApp "c0:outcome-resource-fault"
                                  (LCons (FVar "fault") LNil))
                                (LCons (PApp "c0:receipt-cons"
                                    (LCons (PApp "c0:step-event"
                                        (LCons (FVar "pc") LNil))
                                      (LCons (FVar "receipt") LNil)))
                                  LNil))))
                          LNil)))))))))))))
