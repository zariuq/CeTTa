; Project post-lowering graph diagnostics using origins emitted at helper
; allocation. Equal spans do not identify occurrences or their owning rules.
; List order and multiplicity are retained; unknown diagnostic heads remain
; explicit data. This transformation does not change admission policy.
(gslt-presentation-v1 EbnfDiagnosticProjectionV1
  (signature
    (operator metta-equation 2)
    (operator langdef:expression->list 1)
    (operator langdef:ground-equal 2)
    (operator + 2)
    (operator bnf-v1:text->string 1)
    (operator LCons 2)
    (operator LNil 0)
    (operator BNFDiagnosticsConsV1 2)
    (operator BNFUnreachableDefinitionV1 2)
    (operator BNFUnproductiveDefinitionV1 2)
    (operator ebnf-v1:origins-nil 0)
    (operator ebnf-v1:origins-cons 2)
    (operator ebnf-v1:helper-origin 4)
    (operator ebnf-v1:rule-origin 2)
    (operator EBNF:Diagnostics 1)
    (operator EBNF:DefinitionDiagnostic 3)
    (operator EBNF:ExpressionDiagnostic 6)
    (operator EBNF:UnmappedDiagnostic 1)
    (operator ebnf-v1:project-graph-diagnostics 2)
    (operator ebnf-v1:diagnostic-list 2)
    (operator ebnf-v1:diagnostic 2)
    (operator ebnf-v1:diagnostic-head 3)
    (operator ebnf-v1:diagnostic-unreachable 5)
    (operator ebnf-v1:diagnostic-unproductive 4)
    (operator ebnf-v1:diagnostic-fields 3)
    (operator ebnf-v1:diagnostic-origin 5)
    (operator ebnf-v1:diagnostic-origin-match 7))
  (equations)
  (rewrites
    (rule project-graph-diagnostics
      (head (metta-equation
        (ebnf-v1:project-graph-diagnostics ?origins ?diagnostics)
        (EBNF:Diagnostics (ebnf-v1:diagnostic-list ?origins ?diagnostics))))
      (body))
    (rule diagnostic-list-empty
      (head (metta-equation
        (ebnf-v1:diagnostic-list ?origins BNFDiagnosticsNilV1)
        LNil))
      (body))
    (rule diagnostic-list-cons
      (head (metta-equation
        (ebnf-v1:diagnostic-list ?origins (BNFDiagnosticsConsV1 ?head ?tail))
        (LCons (ebnf-v1:diagnostic ?origins ?head)
          (ebnf-v1:diagnostic-list ?origins ?tail))))
      (body))
    (rule diagnostic-decode
      (head (metta-equation
        (ebnf-v1:diagnostic ?origins ?raw)
        (ebnf-v1:diagnostic-head ?origins ?raw
          (langdef:expression->list ?raw))))
      (body))
    (rule diagnostic-head
      (head (metta-equation
        (ebnf-v1:diagnostic-head ?origins ?raw (LCons ?head ?fields))
        (ebnf-v1:diagnostic-unreachable
          (langdef:ground-equal ?head BNFUnreachableDefinitionV1)
          ?origins ?raw ?head ?fields)))
      (body))
    (rule diagnostic-is-unreachable
      (head (metta-equation
        (ebnf-v1:diagnostic-unreachable
          LangDef:SameValue ?origins ?raw ?head ?fields)
        (ebnf-v1:diagnostic-fields unreachable ?origins ?fields)))
      (body))
    (rule diagnostic-not-unreachable
      (head (metta-equation
        (ebnf-v1:diagnostic-unreachable
          LangDef:DifferentValue ?origins ?raw ?head ?fields)
        (ebnf-v1:diagnostic-unproductive
          (langdef:ground-equal ?head BNFUnproductiveDefinitionV1)
          ?origins ?raw ?fields)))
      (body))
    (rule diagnostic-is-unproductive
      (head (metta-equation
        (ebnf-v1:diagnostic-unproductive
          LangDef:SameValue ?origins ?raw ?fields)
        (ebnf-v1:diagnostic-fields unproductive ?origins ?fields)))
      (body))
    (rule diagnostic-unknown
      (head (metta-equation
        (ebnf-v1:diagnostic-unproductive
          LangDef:DifferentValue ?origins ?raw ?fields)
        (EBNF:UnmappedDiagnostic ?raw)))
      (body))
    (rule diagnostic-fields
      (head (metta-equation
        (ebnf-v1:diagnostic-fields ?kind ?origins
          (LCons ?name (LCons ?span LNil)))
        (ebnf-v1:diagnostic-origin ?origins 0 ?kind ?name ?span)))
      (body))
    (rule diagnostic-authored-definition
      (head (metta-equation
        (ebnf-v1:diagnostic-origin (ebnf-v1:origins-nil)
          ?index ?kind ?name ?span)
        (EBNF:DefinitionDiagnostic ?kind (bnf-v1:text->string ?name) ?span)))
      (body))
    (rule diagnostic-origin-cons
      (head (metta-equation
        (ebnf-v1:diagnostic-origin
          (ebnf-v1:origins-cons
            (ebnf-v1:helper-origin ?found ?operator ?owner ?expression-span) ?tail)
          ?index ?kind ?name ?span)
        (ebnf-v1:diagnostic-origin-match
          (langdef:ground-equal ?name ?found)
          (ebnf-v1:helper-origin ?found ?operator ?owner ?expression-span)
          ?tail ?index ?kind ?name ?span)))
      (body))
    (rule diagnostic-helper-found
      (head (metta-equation
        (ebnf-v1:diagnostic-origin-match LangDef:SameValue
          (ebnf-v1:helper-origin ?found ?operator
            (ebnf-v1:rule-origin ?owner ?owner-span) ?expression-span)
          ?tail ?index ?kind ?name ?span)
        (EBNF:ExpressionDiagnostic ?kind
          (bnf-v1:text->string ?owner) ?operator ?index
          ?owner-span ?expression-span)))
      (body))
    (rule diagnostic-helper-next
      (head (metta-equation
        (ebnf-v1:diagnostic-origin-match LangDef:DifferentValue
          ?origin ?tail ?index ?kind ?name ?span)
        (ebnf-v1:diagnostic-origin ?tail (+ ?index 1) ?kind ?name ?span)))
      (body))))
