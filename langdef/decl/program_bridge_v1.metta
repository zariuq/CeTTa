; DeclV1's import: the parse read as the DeclSyntaxTree it came from and as
; the DeclProgram it denotes.  A sum folds to the left; a call stays a
; constructor.
(gslt-presentation-v1 DeclProgramBridgeV1
  (signature
    (operator metta-equation 2)
    (operator node 2)
    (operator pair 2)
    (operator cons 2)
    (operator cp 1)
    (operator some 1)
    (operator let 3)
    (operator metta-nullary 1)
    (operator cons-atom 2)
    (operator collapse 1)
    (operator langdef:codepoints->string 1)
    (operator LangDef:ImportPrepared 4)
    (operator LangDef:ImportReady 2)
    (operator decl:parse->import 1)
    (operator decl:prepared-raw 1)
    (operator decl:prepared-digest 1)
    (operator decl:empty 1)
    (operator decl:label 1)
    (operator decl:raw-label 1)
    (operator decl:raw->syntax-tree 1)
    (operator decl:syntax-tree->raw 1)
    (operator decl:ast->program 1)
    (operator decl:decls 1)
    (operator decl:decl-of 1)
    (operator decl:name-of 1)
    (operator decl:sum-of 1)
    (operator decl:adds 2)
    (operator decl:app-of 1)
    (operator decl-cst:node 2)
    (operator decl-cst:codepoint 1)
    (operator decl-cst:pair 2)
    (operator decl-cst:cons 2)
    (operator decl-cst:some 1)
    (operator decl-cst:none 0)
    (operator decl-cst:nil 0)
    (operator decl-cst:label-decl-file 0)
    (operator decl-cst:label-decl 0)
    (operator decl-cst:label-sum 0)
    (operator decl-cst:label-call 0)
    (operator decl-cst:label-number 0)
    (operator decl-cst:label-ref 0)
    (operator decl-cst:label-name 0)
    (operator decl-cst:label-digits 0)
    (operator decl:name 1)
    (operator decl:num 1)
    (operator decl:ref 1)
    (operator decl:add 2)
    (operator decl:call 2)
    (operator decl:decl 2)
    (operator decl:decls-nil 0)
    (operator decl:decls-cons 2)
    (operator decl:program 1)
  )
  (equations)
  (rewrites
    (rule eq-empty
      (head (metta-equation
        (decl:empty empty)
        (collapse (metta-nullary empty))))
      (body))
    (rule eq-label-decl-file
      (head (metta-equation
        (decl:label decl-file)
        (decl-cst:label-decl-file)))
      (body))
    (rule eq-raw-label-decl-file
      (head (metta-equation
        (decl:raw-label (decl-cst:label-decl-file))
        decl-file))
      (body))
    (rule eq-label-decl
      (head (metta-equation
        (decl:label decl)
        (decl-cst:label-decl)))
      (body))
    (rule eq-raw-label-decl
      (head (metta-equation
        (decl:raw-label (decl-cst:label-decl))
        decl))
      (body))
    (rule eq-label-sum
      (head (metta-equation
        (decl:label sum)
        (decl-cst:label-sum)))
      (body))
    (rule eq-raw-label-sum
      (head (metta-equation
        (decl:raw-label (decl-cst:label-sum))
        sum))
      (body))
    (rule eq-label-call
      (head (metta-equation
        (decl:label call)
        (decl-cst:label-call)))
      (body))
    (rule eq-raw-label-call
      (head (metta-equation
        (decl:raw-label (decl-cst:label-call))
        call))
      (body))
    (rule eq-label-number
      (head (metta-equation
        (decl:label number)
        (decl-cst:label-number)))
      (body))
    (rule eq-raw-label-number
      (head (metta-equation
        (decl:raw-label (decl-cst:label-number))
        number))
      (body))
    (rule eq-label-ref
      (head (metta-equation
        (decl:label ref)
        (decl-cst:label-ref)))
      (body))
    (rule eq-raw-label-ref
      (head (metta-equation
        (decl:raw-label (decl-cst:label-ref))
        ref))
      (body))
    (rule eq-label-name
      (head (metta-equation
        (decl:label name)
        (decl-cst:label-name)))
      (body))
    (rule eq-raw-label-name
      (head (metta-equation
        (decl:raw-label (decl-cst:label-name))
        name))
      (body))
    (rule eq-label-digits
      (head (metta-equation
        (decl:label digits)
        (decl-cst:label-digits)))
      (body))
    (rule eq-raw-label-digits
      (head (metta-equation
        (decl:raw-label (decl-cst:label-digits))
        digits))
      (body))
    (rule eq-raw-to-tree-node
      (head (metta-equation
        (decl:raw->syntax-tree (node ?label ?payload))
        (decl-cst:node (decl:label ?label) (decl:raw->syntax-tree ?payload))))
      (body))
    (rule eq-raw-to-tree-codepoint
      (head (metta-equation
        (decl:raw->syntax-tree (cp ?value))
        (decl-cst:codepoint ?value)))
      (body))
    (rule eq-raw-to-tree-pair
      (head (metta-equation
        (decl:raw->syntax-tree (pair ?left ?right))
        (decl-cst:pair (decl:raw->syntax-tree ?left) (decl:raw->syntax-tree ?right))))
      (body))
    (rule eq-raw-to-tree-cons
      (head (metta-equation
        (decl:raw->syntax-tree (cons ?first ?rest))
        (decl-cst:cons (decl:raw->syntax-tree ?first) (decl:raw->syntax-tree ?rest))))
      (body))
    (rule eq-raw-to-tree-some
      (head (metta-equation
        (decl:raw->syntax-tree (some ?value))
        (decl-cst:some (decl:raw->syntax-tree ?value))))
      (body))
    (rule eq-raw-to-tree-none
      (head (metta-equation
        (decl:raw->syntax-tree none)
        (decl-cst:none)))
      (body))
    (rule eq-raw-to-tree-nil
      (head (metta-equation
        (decl:raw->syntax-tree nil)
        (decl-cst:nil)))
      (body))
    (rule eq-tree-to-raw-node
      (head (metta-equation
        (decl:syntax-tree->raw (decl-cst:node ?label ?payload))
        (node (decl:raw-label ?label) (decl:syntax-tree->raw ?payload))))
      (body))
    (rule eq-tree-to-raw-codepoint
      (head (metta-equation
        (decl:syntax-tree->raw (decl-cst:codepoint ?value))
        (cp ?value)))
      (body))
    (rule eq-tree-to-raw-pair
      (head (metta-equation
        (decl:syntax-tree->raw (decl-cst:pair ?left ?right))
        (pair (decl:syntax-tree->raw ?left) (decl:syntax-tree->raw ?right))))
      (body))
    (rule eq-tree-to-raw-cons
      (head (metta-equation
        (decl:syntax-tree->raw (decl-cst:cons ?first ?rest))
        (cons (decl:syntax-tree->raw ?first) (decl:syntax-tree->raw ?rest))))
      (body))
    (rule eq-tree-to-raw-some
      (head (metta-equation
        (decl:syntax-tree->raw (decl-cst:some ?value))
        (some (decl:syntax-tree->raw ?value))))
      (body))
    (rule eq-tree-to-raw-none
      (head (metta-equation
        (decl:syntax-tree->raw (decl-cst:none))
        none))
      (body))
    (rule eq-tree-to-raw-nil
      (head (metta-equation
        (decl:syntax-tree->raw (decl-cst:nil))
        nil))
      (body))
    (rule eq-ast-to-program
      (head (metta-equation
        (decl:ast->program (node decl-file ?decls))
        (decl:program (decl:decls ?decls))))
      (body))
    (rule eq-decls-nil
      (head (metta-equation
        (decl:decls nil)
        (decl:decls-nil)))
      (body))
    (rule eq-decls-cons
      (head (metta-equation
        (decl:decls (cons ?decl ?rest))
        (decl:decls-cons (decl:decl-of ?decl) (decl:decls ?rest))))
      (body))
    (rule eq-decl
      (head (metta-equation
        (decl:decl-of (node decl (pair ?name ?sum)))
        (decl:decl (decl:name-of ?name) (decl:sum-of ?sum))))
      (body))
    (rule eq-name
      (head (metta-equation
        (decl:name-of (node name ?codepoints))
        (decl:name (langdef:codepoints->string ?codepoints))))
      (body))
    (rule eq-sum
      (head (metta-equation
        (decl:sum-of (node sum (pair ?first ?rest)))
        (decl:adds (decl:app-of ?first) ?rest)))
      (body))
    (rule eq-adds-nil
      (head (metta-equation
        (decl:adds ?sum nil)
        ?sum))
      (body))
    (rule eq-adds-cons
      (head (metta-equation
        (decl:adds ?sum (cons ?app ?rest))
        (decl:adds (decl:add ?sum (decl:app-of ?app)) ?rest)))
      (body))
    (rule eq-call
      (head (metta-equation
        (decl:app-of (node call (pair ?name ?sum)))
        (decl:call (decl:name-of ?name) (decl:sum-of ?sum))))
      (body))
    (rule eq-number
      (head (metta-equation
        (decl:app-of (node number (node digits ?codepoints)))
        (decl:num (langdef:codepoints->string ?codepoints))))
      (body))
    (rule eq-ref
      (head (metta-equation
        (decl:app-of (node ref ?name))
        (decl:ref (decl:name-of ?name))))
      (body))
    (rule eq-prepared-raw
      (head (metta-equation
        (decl:prepared-raw
          (LangDef:ImportPrepared DeclV1 ?pack ?digest (metta-nullary ?ast)))
        ?ast))
      (body))
    (rule eq-prepared-digest
      (head (metta-equation
        (decl:prepared-digest (LangDef:ImportPrepared DeclV1 ?pack ?digest ?values))
        ?digest))
      (body))
    (rule eq-parse-to-import
      (head (metta-equation
        (decl:parse->import
          (LangDef:ImportPrepared DeclV1 ?pack ?digest (metta-nullary ?ast)))
        (let ?tree (decl:raw->syntax-tree ?ast)
          (let ?program (decl:ast->program ?ast)
            (let ?empty (decl:empty empty)
              (LangDef:ImportReady ?tree (cons-atom ?program ?empty)))))))
      (body))
  ))
