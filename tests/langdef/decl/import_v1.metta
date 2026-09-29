; Importing DeclV1 text through its manifest's generated program.
;
; The manifest declares `(import-entry decl:parse->import)` with
; `(result-category "Program")`.  An import reads the text, gives the parse to
; the program, checks the syntax tree against the source LanguageDef and the
; program against the result LanguageDef, and publishes exactly the one
; program into &self.  A rejected text or a result of another category leaves
; &self as it was.  A call in the text stays a constructor of the program.

!(import! &self langdef)

(= (test:imported $result)
   (case $result
     (((LangDef:Imported DeclV1 $digest 1) True)
      ($_ False))))

(= (test:parse-rejected $result)
   (case $result
     (((return (LangDef:ParseRejected DeclV1 $digest $offset $expected)) True)
      ((LangDef:ParseRejected DeclV1 $digest $offset $expected) True)
      ($_ False))))

(= (test:published-programs)
   (collapse (match &self (decl:program $decls) found)))

(= (test:published-trees)
   (collapse (match &self (decl-cst:node $label $payload) found)))

(= (test:expected-program)
   (decl:program
     (decl:decls-cons
       (decl:decl (decl:name "x") (decl:add (decl:num "1") (decl:num "2")))
       (decl:decls-cons
         (decl:decl (decl:name "y")
           (decl:add (decl:call (decl:name "f") (decl:ref (decl:name "x")))
                     (decl:num "30")))
         (decl:decls-nil)))))

!(bind! &decl (langdef:load "langdef/decl/langdef.metta"))
!(bind! &prepared (langdef:prepare-import-file &decl "tests/langdef/decl/two_decls.decl"))
!(bind! &raw (decl:prepared-raw &prepared))

; ------------------------------------------------- the syntax tree, both ways --

!(bind! &forward
   (langdef:load-structural-tree-relabel
     "langdef/decl/program_bridge_v1.metta" decl:raw->syntax-tree decl:label))
!(bind! &inverse
   (langdef:load-structural-tree-relabel
     "langdef/decl/program_bridge_v1.metta" decl:syntax-tree->raw decl:raw-label))
!(bind! &syntax-tree (langdef:structural-tree-relabel &forward &raw))

; 1: relabelling the parse and back gives the parse.
!(assertEqual (langdef:structural-tree-relabel &inverse &syntax-tree) &raw)

; ------------------------------------------ the program, by the bridge alone --

!(bind! &plan
   (langdef:load-deterministic-equations
     (LangDef:EquationSources "langdef/decl/program_bridge_v1.metta")))
!(bind! &program
   (langdef:run-deterministic-equations &plan
     (LangDef:DeterministicCall decl:ast->program &raw)))

; 2: the bridge's program: sums fold to the left, the call is a constructor.
!(assertEqual &program (test:expected-program))

!(bind! &tree-language (langdef:load-language "langdef/decl/syntax_tree_v1.metta"))
!(bind! &program-language (langdef:load-language "langdef/decl/program_v1.metta"))

; 3: the syntax tree is a term of the source LanguageDef.
!(assertEqual (langdef:admit-term &tree-language "SyntaxTree" &syntax-tree)
   (LangDef:TermAdmitted))

; 4: the program is a term of the result LanguageDef.
!(assertEqual (langdef:admit-term &program-language "Program" &program)
   (LangDef:TermAdmitted))

; 5: a declaration is a term of the result LanguageDef but not a Program.
!(assertEqual
   (case (langdef:admit-term &program-language "Program"
           (decl:decl (decl:name "z") (decl:num "0")))
     (((LangDef:TermAdmitted) False)
      ($_ True)))
   True)

; ------------------------------------------------------------ publication --

!(bind! &before-import (count-atoms &self))
!(bind! &imported (langdef:import-file &decl "tests/langdef/decl/two_decls.decl"))

; 6: the import reports publishing exactly one value.
!(assertEqual (test:imported &imported) True)

; 7: exactly one atom reached &self.
!(assertEqual (- (count-atoms &self) &before-import) 1)

; 8: it is the program the bridge alone computes.
!(assertEqual (test:published-programs) (found))
!(assertEqual (match &self &program True) True)

; 9: the syntax tree was checked, not published.
!(assertEqual (test:published-trees) ())

; ----------------------------------------------------- a rejected import --

!(bind! &before-rejected (count-atoms &self))
!(bind! &rejected (langdef:import-file &decl "tests/langdef/decl/missing_operand.decl"))

; 10: a text the parser rejects is reported as rejected.
!(assertEqual (test:parse-rejected &rejected) True)

; 11: and leaves &self as it was, the program included.
!(assertEqual (count-atoms &self) &before-rejected)
!(assertEqual (match &self &program True) True)

; ---------------------------------------- a result of another category --

!(bind! &wrong-result (decl:decl (decl:name "z") (decl:num "0")))

; 12: a declaration is not published as the import's Program, and the failed
; publication adds nothing.
!(assertEqual (collapse (match &self &wrong-result True)) ())
!(assertEqual
   (case
     (__cetta_lib_langdef_publish &decl
       (LangDef:ImportReady &syntax-tree (cons-atom &wrong-result (decl:empty empty))))
     (((return (LangDef:PublishFailure $_source $_message)) True)
      ($_ False)))
   True)
!(assertEqual (collapse (match &self &wrong-result True)) ())
!(assertEqual (count-atoms &self) &before-rejected)

!(assertEqual (langdef:close-structural-tree-relabel &forward) True)
!(assertEqual (langdef:close-structural-tree-relabel &inverse) True)
!(assertEqual (langdef:close-language &tree-language) True)
!(assertEqual (langdef:close-language &program-language) True)
!(assertEqual (langdef:close &decl) True)

!(println! (DeclImportV1Summary 12 12 0))
