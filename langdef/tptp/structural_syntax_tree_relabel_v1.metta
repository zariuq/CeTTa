(gslt-presentation-v1 TptpStructuralSyntaxTreeRelabelV1
  (signature
    (operator metta-equation 2)
    (operator node 2)
    (operator SourceSpanNodeV1 4)
    (operator pair 2)
    (operator cons 2)
    (operator some 1)
    (operator cp 1)

    (operator tptp:raw->syntax-tree 1)
    (operator tptp:syntax-tree->raw 1)
    (operator tptp:syntax-tree-label 1)
    (operator tptp:syntax-tree-raw-label 1)

    (operator tptp-cst:node 2)
    (operator tptp-cst:spanned-node 4)
    (operator tptp-cst:codepoint 1)
    (operator tptp-cst:pair 2)
    (operator tptp-cst:cons 2)
    (operator tptp-cst:some 1)
    (operator tptp-cst:none 0)
    (operator tptp-cst:nil 0))
  (equations)
  (rewrites
    (rule eq-raw-to-syntax-tree-node
      (head
        (metta-equation
          (tptp:raw->syntax-tree (node ?label ?payload))
          (tptp-cst:node
            (tptp:syntax-tree-label ?label)
            (tptp:raw->syntax-tree ?payload))))
      (body))
    (rule eq-raw-to-syntax-tree-spanned-node
      (head
        (metta-equation
          (tptp:raw->syntax-tree
            (SourceSpanNodeV1 ?label ?start ?stop ?payload))
          (tptp-cst:spanned-node
            (tptp:syntax-tree-label ?label)
            ?start ?stop
            (tptp:raw->syntax-tree ?payload))))
      (body))
    (rule eq-raw-to-syntax-tree-codepoint
      (head
        (metta-equation
          (tptp:raw->syntax-tree (cp ?value))
          (tptp-cst:codepoint ?value)))
      (body))
    (rule eq-raw-to-syntax-tree-pair
      (head
        (metta-equation
          (tptp:raw->syntax-tree (pair ?left ?right))
          (tptp-cst:pair
            (tptp:raw->syntax-tree ?left)
            (tptp:raw->syntax-tree ?right))))
      (body))
    (rule eq-raw-to-syntax-tree-cons
      (head
        (metta-equation
          (tptp:raw->syntax-tree (cons ?first ?rest))
          (tptp-cst:cons
            (tptp:raw->syntax-tree ?first)
            (tptp:raw->syntax-tree ?rest))))
      (body))
    (rule eq-raw-to-syntax-tree-some
      (head
        (metta-equation
          (tptp:raw->syntax-tree (some ?value))
          (tptp-cst:some (tptp:raw->syntax-tree ?value))))
      (body))
    (rule eq-raw-to-syntax-tree-none
      (head
        (metta-equation
          (tptp:raw->syntax-tree none)
          (tptp-cst:none)))
      (body))
    (rule eq-raw-to-syntax-tree-nil
      (head
        (metta-equation
          (tptp:raw->syntax-tree nil)
          (tptp-cst:nil)))
      (body))

    (rule eq-syntax-tree-to-raw-node
      (head
        (metta-equation
          (tptp:syntax-tree->raw (tptp-cst:node ?label ?payload))
          (node
            (tptp:syntax-tree-raw-label ?label)
            (tptp:syntax-tree->raw ?payload))))
      (body))
    (rule eq-syntax-tree-to-raw-spanned-node
      (head
        (metta-equation
          (tptp:syntax-tree->raw
            (tptp-cst:spanned-node ?label ?start ?stop ?payload))
          (SourceSpanNodeV1
            (tptp:syntax-tree-raw-label ?label)
            ?start ?stop
            (tptp:syntax-tree->raw ?payload))))
      (body))
    (rule eq-syntax-tree-to-raw-codepoint
      (head
        (metta-equation
          (tptp:syntax-tree->raw (tptp-cst:codepoint ?value))
          (cp ?value)))
      (body))
    (rule eq-syntax-tree-to-raw-pair
      (head
        (metta-equation
          (tptp:syntax-tree->raw (tptp-cst:pair ?left ?right))
          (pair
            (tptp:syntax-tree->raw ?left)
            (tptp:syntax-tree->raw ?right))))
      (body))
    (rule eq-syntax-tree-to-raw-cons
      (head
        (metta-equation
          (tptp:syntax-tree->raw (tptp-cst:cons ?first ?rest))
          (cons
            (tptp:syntax-tree->raw ?first)
            (tptp:syntax-tree->raw ?rest))))
      (body))
    (rule eq-syntax-tree-to-raw-some
      (head
        (metta-equation
          (tptp:syntax-tree->raw (tptp-cst:some ?value))
          (some (tptp:syntax-tree->raw ?value))))
      (body))
    (rule eq-syntax-tree-to-raw-none
      (head
        (metta-equation
          (tptp:syntax-tree->raw (tptp-cst:none))
          none))
      (body))
    (rule eq-syntax-tree-to-raw-nil
      (head
        (metta-equation
          (tptp:syntax-tree->raw (tptp-cst:nil))
          nil))
      (body))))
