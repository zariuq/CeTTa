; The compact form of TPTP: a morphism from the canonical terms of the
; official grammar to ordinary MeTTa data.
;
; A canonical term (lib_bnf over the pinned SyntaxBNF) writes each grammar
; alternative as (bnf NAME child...), each token as (bnf TOKEN "text"), each
; literal alternative as its text and each list as a list value.  This file
; gives the value of every constructor, literal and token of that language by
; one equation of tptp-compact:v, so the whole interpretation of TPTP is read
; here.  The equations run in the deterministic equation runner: every
; left-hand side is exclusive and nothing else interprets the terms.
;
; Records
;   (fof NAME ROLE FORMULA SOURCE? INFO?), and the same for thf tff tcf cnf
;   tpi; the annotations, when present, trail the formula.  A role is a bare
;   symbol, a refined role lower-general is (lower GENERAL).
;   (incl "FILE" SELECTION? SPACE?), SELECTION a list of names or *.
; Formulas and terms
;   Connectives are glyphs: ¬ ∧ ∨ → ← ↔ ⊕ ⊼ ⊽, equality ≈ ≉; quantifiers
;   ∀ ∃ λ, choice ε and description ι, type quantifiers Π Σ.  Types use ⇒ ×
;   ⊎, typing is (: X T), subtype ⊑, definition ≡, assignment ≔, sequent ⊢,
;   the modal short connectives [.] <.> are □ ◇.  Every other TPTP built-in
;   spelling is the defined symbol of that spelling, such as the index #.  A
;   long connective is its name and parameters: {$box} is ($box) and
;   {$box(#1)} is ($box (# 1)), in the defined spellings of the policy.
;   A binder list is a list: (∀ [X Y] F), (λ [(: X T)] F).
;   An application is its head followed by its arguments: p(X) is (p X).
;   @ associates to the left, so an application in function position,
;   parenthesized or not, extends it: f@g@X and (f@g)@X are (f g X), while
;   f@(g@X) is (f (g X)).  The chains a|b|c, a&b&c, a*b*c and a+b+c are one
;   expression each; explicit parentheses keep their nesting, and > nests to
;   the right as the grammar does.  A connective used as a term, (&), is its
;   glyph, so (&)@p@q and p&q are both (∧ p q).
;   A clause is (∨ LITERAL...), also for one literal.
;   Every list is a list value, which never equals an application: a tuple,
;   a general list, a list of sources, binders, the typings and definitions
;   of $let, useful info, parents and selections, [A B], and [] when empty.
;   A document is the list of its records.  $let(T, D, F) is ($let TYPINGS
;   DEFINITIONS F) with both lists, also for a single typing or definition.
; Leaves (tagged): (word "p") for cat and 'cat' alike, (var "X"),
;   (defined "i") for $i, (system "s") for $$s, (distinct "d"), integers as
;   numbers, (rat N D) in lowest terms, (real "1.02") with the canonical
;   decimal spelling of its value; file names are strings.
(gslt-presentation-v1 TptpCompactV1
  (signature
    (operator : 2)
    (operator < 2)
    (operator LCons 2)
    (operator bnf 1)
    (operator bnf 2)
    (operator bnf 3)
    (operator bnf 4)
    (operator bnf 5)
    (operator cons 2)
    (operator cp 1)
    (operator defined 1)
    (operator distinct 1)
    (operator metta-equation 2)
    (operator rat 2)
    (operator real 1)
    (operator system 1)
    (operator var 1)
    (operator word 1)
    (operator LangDef:ExpressionValue 1)
    (operator LangDef:ListValue 1)
    (operator LangDef:Float 1)
    (operator LangDef:StringValue 1)
    (operator langdef:decimal-integer->number 1)
    (operator langdef:decimal-rational->value 1)
    (operator langdef:decimal-real->canonical 1)
    (operator langdef:decimal-real->float 1)
    (operator langdef:expression->list 1)
    (operator langdef:ground-equal 2)
    (operator langdef:list->expression 1)
    (operator langdef:list->list-value 1)
    (operator langdef:value-view 1)
    (operator + 2)
    (operator str:all-in? 2)
    (operator str:byte-length 1)
    (operator str:byte-slice 3)
    (operator str:concat 2)
    (operator str:lower-word? 1)
    (operator str:set 1)
    (operator str:set-member? 2)
    (operator str:string->symbol 1)
    (operator str:unescape 2)
    (operator tptp-compact:after-sigil 2)
            (operator tptp-compact:append 2)
    (operator tptp-compact:apply 3)
    (operator tptp-compact:bare-or-word 2)
    (operator tptp-compact:clause 2)
    (operator tptp-compact:clause-at 3)
    (operator tptp-compact:defined 2)
    (operator tptp-compact:definitions 2)
    (operator tptp-compact:definitions-by 3)
    (operator tptp-compact:disjuncts 2)
                (operator tptp-compact:embedded 3)
    (operator tptp-compact:expression 1)
    (operator tptp-compact:file 2)
    (operator tptp-compact:file-name 1)
    (operator tptp-compact:fixed 4)
    (operator tptp-compact:float-or-exact 2)
    (operator tptp-compact:host-literal-variables 0)
    (operator tptp-compact:info 2)
    (operator tptp-compact:items 3)
        (operator tptp-compact:kind 1)
    (operator tptp-compact:kind-after 2)
    (operator tptp-compact:kind-of 1)
    (operator tptp-compact:left-chain 4)
    (operator tptp-compact:left-chain-at 5)
    (operator tptp-compact:let 4)
    (operator tptp-compact:list-value 1)
        (operator tptp-compact:map 2)
    (operator tptp-compact:one-of 2)
    (operator tptp-compact:one-of-at 3)
    (operator tptp-compact:operator 4)
        (operator tptp-compact:parent 2)
    (operator tptp-compact:parents 2)
    (operator tptp-compact:quantify 4)
    (operator tptp-compact:quoted-at 3)
    (operator tptp-compact:quoted-word 2)
    (operator tptp-compact:rational 1)
    (operator tptp-compact:record 6)
    (operator tptp-compact:reserved-words 0)
    (operator tptp-compact:role 2)
    (operator tptp-compact:sigil 3)
    (operator tptp-compact:sigil-at 4)
    (operator tptp-compact:single 1)
    (operator tptp-compact:spine 2)
    (operator tptp-compact:spine-at 3)
    (operator tptp-compact:spine-of 3)
    (operator tptp-compact:symbol 1)
    (operator tptp-compact:system 2)
    (operator tptp-compact:tcf 2)
    (operator tptp-compact:tcf-at 3)
        (operator tptp-compact:typings 2)
    (operator tptp-compact:typings-by 3)
    (operator tptp-compact:unary 3)
            (operator tptp-compact:unquote 1)
    (operator tptp-compact:v 2)
    (operator tptp-compact:elements 2)
    (operator tptp-compact:variable 2)
    (operator tptp-compact:variable-at 2)
        (operator tptp-compact:word 2)
    )
  (equations)
  (rewrites
    ; ------------------------------------------------------------ entry
    (rule file
      (head (metta-equation (tptp-compact:file ?policy ?inputs)
                            (tptp-compact:elements ?policy ?inputs)))
      (body))

    ; ------------------------------------------------------- list helpers
    (rule map-nil (head (metta-equation (tptp-compact:map ?policy LNil) LNil)) (body))
    (rule map-cons
      (head (metta-equation (tptp-compact:map ?policy (LCons ?item ?rest))
                            (LCons (tptp-compact:v ?policy ?item) (tptp-compact:map ?policy ?rest))))
      (body))
    (rule expression (head (metta-equation (tptp-compact:expression ?list) (langdef:list->expression ?list))) (body))
    (rule list-value (head (metta-equation (tptp-compact:list-value ?list) (langdef:list->list-value ?list))) (body))
    ; The list of the compact values of the elements of a list, in order.
    (rule elements
      (head (metta-equation (tptp-compact:elements ?policy ?list)
                            (tptp-compact:list-value (tptp-compact:map ?policy (langdef:expression->list ?list)))))
      (body))
    ; x followed by the further x of x (, x)*, a repetition's list node.
    (rule items
      (head (metta-equation (tptp-compact:items ?policy ?first ?rest)
                            (tptp-compact:list-value
                              (LCons (tptp-compact:v ?policy ?first)
                                     (langdef:expression->list (tptp-compact:v ?policy ?rest))))))
      (body))
    (rule single (head (metta-equation (tptp-compact:single ?value) (tptp-compact:list-value (LCons ?value LNil)))) (body))
    (rule append-nil (head (metta-equation (tptp-compact:append LNil ?tail) ?tail)) (body))
    (rule append-cons
      (head (metta-equation (tptp-compact:append (LCons ?item ?rest) ?tail)
                            (LCons ?item (tptp-compact:append ?rest ?tail))))
      (body))
    ; A left-nested chain ((x1 o x2) o ...) o xn, whose value is the value of
    ; its first link followed by the later operands: the chain is walked down
    ; its left spine once and the later operands gathered in order, so a chain
    ; of n operands takes n steps.  (head a b) followed by c d is (head a b c d).
    (rule left-chain
      (head (metta-equation (tptp-compact:left-chain ?policy ?link ?chain ?later)
                            (tptp-compact:left-chain-at ?policy ?link
                              (langdef:ground-equal (tptp-compact:kind ?chain) ?link) ?chain ?later)))
      (body))
    (rule left-chain-link
      (head (metta-equation (tptp-compact:left-chain-at ?policy ?link LangDef:SameValue (bnf ?name ?chain ?operand) ?later)
                            (tptp-compact:left-chain ?policy ?link ?chain (LCons (tptp-compact:v ?policy ?operand) ?later))))
      (body))
    (rule left-chain-first
      (head (metta-equation (tptp-compact:left-chain-at ?policy ?link LangDef:DifferentValue ?first ?later)
                            (tptp-compact:expression
                              (tptp-compact:append (langdef:expression->list (tptp-compact:v ?policy ?first)) ?later))))
      (body))
    ; The head applied to the compact values of its argument list.
    (rule apply
      (head (metta-equation (tptp-compact:apply ?policy ?head ?arguments)
                            (tptp-compact:expression
                              (LCons (tptp-compact:v ?policy ?head)
                                     (langdef:expression->list (tptp-compact:v ?policy ?arguments))))))
      (body))
    (rule unary
      (head (metta-equation (tptp-compact:unary ?policy ?connective ?operand)
                            (tptp-compact:expression
                              (LCons (tptp-compact:v ?policy ?connective)
                                     (LCons (tptp-compact:v ?policy ?operand) LNil)))))
      (body))
    (rule operator
      (head (metta-equation (tptp-compact:operator ?policy ?connective ?left ?right)
                            (tptp-compact:expression
                              (LCons (tptp-compact:v ?policy ?connective)
                                     (LCons (tptp-compact:v ?policy ?left)
                                            (LCons (tptp-compact:v ?policy ?right) LNil))))))
      (body))
    (rule fixed
      (head (metta-equation (tptp-compact:fixed ?glyph ?policy ?left ?right)
                            (tptp-compact:expression
                              (LCons ?glyph (LCons (tptp-compact:v ?policy ?left)
                                                   (LCons (tptp-compact:v ?policy ?right) LNil))))))
      (body))
    (rule quantify
      (head (metta-equation (tptp-compact:quantify ?policy ?quantifier ?variables ?body)
                            (tptp-compact:expression
                              (LCons (tptp-compact:v ?policy ?quantifier)
                                     (LCons (tptp-compact:v ?policy ?variables)
                                            (LCons (tptp-compact:v ?policy ?body) LNil))))))
      (body))

    ; -------------------------------------------------------------- lists
    ; A list node is the list of the compact values of its elements.
    (rule c-TPTP_file*1 (head (metta-equation (tptp-compact:v ?policy (bnf TPTP_file*1 ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-general_terms*1 (head (metta-equation (tptp-compact:v ?policy (bnf general_terms*1 ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-parent_list*1 (head (metta-equation (tptp-compact:v ?policy (bnf parent_list*1 ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-fof_formula_tuple_list*1 (head (metta-equation (tptp-compact:v ?policy (bnf fof_formula_tuple_list*1 ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-tff_arguments*1 (head (metta-equation (tptp-compact:v ?policy (bnf tff_arguments*1 ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-thf_formula_list*1 (head (metta-equation (tptp-compact:v ?policy (bnf thf_formula_list*1 ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-fof_arguments (head (metta-equation (tptp-compact:v ?policy (bnf fof_arguments ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-fof_variable_list (head (metta-equation (tptp-compact:v ?policy (bnf fof_variable_list ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-name_list (head (metta-equation (tptp-compact:v ?policy (bnf name_list ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-sources (head (metta-equation (tptp-compact:v ?policy (bnf sources ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-tff_variable_list (head (metta-equation (tptp-compact:v ?policy (bnf tff_variable_list ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-tff_type_list (head (metta-equation (tptp-compact:v ?policy (bnf tff_type_list ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-tff_type_arguments (head (metta-equation (tptp-compact:v ?policy (bnf tff_type_arguments ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-tff_atom_typing_list (head (metta-equation (tptp-compact:v ?policy (bnf tff_atom_typing_list ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-txf_let_defn_list (head (metta-equation (tptp-compact:v ?policy (bnf txf_let_defn_list ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-thf_variable_list (head (metta-equation (tptp-compact:v ?policy (bnf thf_variable_list ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-thf_atom_typing_list (head (metta-equation (tptp-compact:v ?policy (bnf thf_atom_typing_list ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-thf_let_defn_list (head (metta-equation (tptp-compact:v ?policy (bnf thf_let_defn_list ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-nxf_parameter_list (head (metta-equation (tptp-compact:v ?policy (bnf nxf_parameter_list ?list)) (tptp-compact:elements ?policy ?list))) (body))
    (rule c-nhf_parameter_list (head (metta-equation (tptp-compact:v ?policy (bnf nhf_parameter_list ?list)) (tptp-compact:elements ?policy ?list))) (body))

    ; ----------------------------------------------------- canonical kinds
    ; The constructor of a canonical node; lists and literals are named by
    ; their kind.  A node is (bnf NAME child...) and a list is a list value.
    (rule kind (head (metta-equation (tptp-compact:kind ?term) (tptp-compact:kind-of (langdef:value-view ?term)))) (body))
    (rule kind-literal (head (metta-equation (tptp-compact:kind-of (LangDef:StringValue ?text)) tptp-compact:literal)) (body))
    (rule kind-list (head (metta-equation (tptp-compact:kind-of (LangDef:ListValue ?items)) tptp-compact:list)) (body))
    (rule kind-expression
      (head (metta-equation (tptp-compact:kind-of (LangDef:ExpressionValue (LCons ?first ?rest)))
                            (tptp-compact:kind-after (langdef:ground-equal ?first bnf) ?rest)))
      (body))
    (rule kind-node (head (metta-equation (tptp-compact:kind-after LangDef:SameValue (LCons ?name ?children)) ?name)) (body))
    ; Whether a value is one of a list of values.
    (rule one-of-none (head (metta-equation (tptp-compact:one-of ?value LNil) LangDef:DifferentValue)) (body))
    (rule one-of-next
      (head (metta-equation (tptp-compact:one-of ?value (LCons ?candidate ?rest))
                            (tptp-compact:one-of-at (langdef:ground-equal ?value ?candidate) ?value ?rest)))
      (body))
    (rule one-of-found (head (metta-equation (tptp-compact:one-of-at LangDef:SameValue ?value ?rest) LangDef:SameValue)) (body))
    (rule one-of-rest (head (metta-equation (tptp-compact:one-of-at LangDef:DifferentValue ?value ?rest) (tptp-compact:one-of ?value ?rest))) (body))

    ; ------------------------------------------------------------ records
    (rule record
      (head (metta-equation (tptp-compact:record ?family ?policy ?name ?role ?formula ?annotations)
                            (tptp-compact:expression
                              (LCons ?family
                                (LCons (tptp-compact:v ?policy ?name)
                                  (LCons (tptp-compact:role ?policy ?role)
                                    (LCons ?formula (tptp-compact:v ?policy ?annotations))))))))
      (body))
    (rule c-tpi_annotated (head (metta-equation (tptp-compact:v ?policy (bnf tpi_annotated ?n ?r ?f ?a)) (tptp-compact:record tpi ?policy ?n ?r (tptp-compact:v ?policy ?f) ?a))) (body))
    (rule c-thf_annotated (head (metta-equation (tptp-compact:v ?policy (bnf thf_annotated ?n ?r ?f ?a)) (tptp-compact:record thf ?policy ?n ?r (tptp-compact:v ?policy ?f) ?a))) (body))
    (rule c-tff_annotated (head (metta-equation (tptp-compact:v ?policy (bnf tff_annotated ?n ?r ?f ?a)) (tptp-compact:record tff ?policy ?n ?r (tptp-compact:v ?policy ?f) ?a))) (body))
    (rule c-tcf_annotated (head (metta-equation (tptp-compact:v ?policy (bnf tcf_annotated ?n ?r ?f ?a)) (tptp-compact:record tcf ?policy ?n ?r (tptp-compact:tcf ?policy ?f) ?a))) (body))
    (rule c-fof_annotated (head (metta-equation (tptp-compact:v ?policy (bnf fof_annotated ?n ?r ?f ?a)) (tptp-compact:record fof ?policy ?n ?r (tptp-compact:v ?policy ?f) ?a))) (body))
    (rule c-cnf_annotated (head (metta-equation (tptp-compact:v ?policy (bnf cnf_annotated ?n ?r ?f ?a)) (tptp-compact:record cnf ?policy ?n ?r (tptp-compact:clause ?policy ?f) ?a))) (body))
    (rule c-annotations
      (head (metta-equation (tptp-compact:v ?policy (bnf annotations ?source ?info))
                            (LCons (tptp-compact:v ?policy ?source) (tptp-compact:v ?policy ?info))))
      (body))
    (rule c-nothing (head (metta-equation (tptp-compact:v ?policy (bnf nothing)) LNil)) (body))
    (rule c-optional_info (head (metta-equation (tptp-compact:v ?policy (bnf optional_info ?useful)) (LCons (tptp-compact:info ?policy ?useful) LNil))) (body))
    (rule role-word (head (metta-equation (tptp-compact:role ?policy (bnf lower_word ?text)) (str:string->symbol ?text))) (body))
    (rule role-refined (head (metta-equation (tptp-compact:role ?policy (bnf formula_role ?word ?term)) (tptp-compact:v ?policy (bnf formula_role ?word ?term)))) (body))
    (rule c-formula_role
      (head (metta-equation (tptp-compact:v ?policy (bnf formula_role ?word ?term))
                            (tptp-compact:expression (LCons (tptp-compact:role ?policy ?word) (LCons (tptp-compact:v ?policy ?term) LNil)))))
      (body))

    ; ---------------------------------------------------------- clauses
    ; A clause is the disjunction of its literals; parentheses around it are
    ; layout.  Each reader profile gives the disjuncts of its disjunctions.
    (rule clause
      (head (metta-equation (tptp-compact:clause ?policy ?formula)
                            (tptp-compact:clause-at ?policy (langdef:ground-equal (tptp-compact:kind ?formula) cnf_formula) ?formula)))
      (body))
    (rule clause-parenthesized (head (metta-equation (tptp-compact:clause-at ?policy LangDef:SameValue ?formula) (tptp-compact:v ?policy ?formula))) (body))
    (rule clause-disjunction
      (head (metta-equation (tptp-compact:clause-at ?policy LangDef:DifferentValue ?disjunction)
                            (tptp-compact:expression (LCons ∨ (tptp-compact:disjuncts ?policy ?disjunction)))))
      (body))
    (rule c-cnf_formula (head (metta-equation (tptp-compact:v ?policy (bnf cnf_formula ?inner)) (tptp-compact:clause ?policy ?inner))) (body))
    (rule c-cnf_literal/1 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_literal/1 ?atom)) (tptp-compact:expression (LCons ¬ (LCons (tptp-compact:v ?policy ?atom) LNil))))) (body))
    (rule c-cnf_literal/2 (head (metta-equation (tptp-compact:v ?policy (bnf cnf_literal/2 ?atom)) (tptp-compact:expression (LCons ¬ (LCons (tptp-compact:v ?policy ?atom) LNil))))) (body))
    ; A TCF formula is a clause, a universally quantified TCF formula or a typing.
    (rule tcf
      (head (metta-equation (tptp-compact:tcf ?policy ?formula)
                            (tptp-compact:tcf-at ?policy
                              (tptp-compact:one-of (tptp-compact:kind ?formula)
                                (LCons tcf_quantified_formula (LCons tff_atom_typing/0 (LCons tff_atom_typing/1 LNil))))
                              ?formula)))
      (body))
    (rule tcf-value (head (metta-equation (tptp-compact:tcf-at ?policy LangDef:SameValue ?formula) (tptp-compact:v ?policy ?formula))) (body))
    (rule tcf-clause (head (metta-equation (tptp-compact:tcf-at ?policy LangDef:DifferentValue ?formula) (tptp-compact:clause ?policy ?formula))) (body))
    (rule c-tcf_quantified_formula
      (head (metta-equation (tptp-compact:v ?policy (bnf tcf_quantified_formula ?variables ?formula))
                            (tptp-compact:expression (LCons ∀ (LCons (tptp-compact:v ?policy ?variables) (LCons (tptp-compact:tcf ?policy ?formula) LNil))))))
      (body))

    ; ------------------------------------------------------------ include
    (rule c-include
      (head (metta-equation (tptp-compact:v ?policy (bnf include ?file ?optionals))
                            (tptp-compact:expression (LCons incl (LCons (tptp-compact:file-name ?file) (tptp-compact:v ?policy ?optionals))))))
      (body))
    (rule file-name-lower (head (metta-equation (tptp-compact:file-name (bnf lower_word ?text)) ?text)) (body))
    (rule file-name-quoted (head (metta-equation (tptp-compact:file-name (bnf single_quoted ?text)) (tptp-compact:unquote ?text))) (body))
    (rule file-name-back-quoted (head (metta-equation (tptp-compact:file-name (bnf back_quoted ?text)) ?text)) (body))
    (rule c-include_optionals/1 (head (metta-equation (tptp-compact:v ?policy (bnf include_optionals/1 ?selection)) (LCons (tptp-compact:v ?policy ?selection) LNil))) (body))
    (rule c-include_optionals/2
      (head (metta-equation (tptp-compact:v ?policy (bnf include_optionals/2 ?selection ?space))
                            (LCons (tptp-compact:v ?policy ?selection) (LCons (tptp-compact:v ?policy ?space) LNil))))
      (body))
    (rule c-formula_selection (head (metta-equation (tptp-compact:v ?policy (bnf formula_selection ?names)) (tptp-compact:v ?policy ?names))) (body))

    ; ------------------------------------------------------------ sources
    (rule c-source (head (metta-equation (tptp-compact:v ?policy (bnf source ?sources)) (tptp-compact:v ?policy ?sources))) (body))
    (rule c-inference_record
      (head (metta-equation (tptp-compact:v ?policy (bnf inference_record ?rule ?info ?parents))
                            (tptp-compact:expression (LCons inference (LCons (tptp-compact:v ?policy ?rule) (LCons (tptp-compact:info ?policy ?info) (LCons (tptp-compact:parents ?policy ?parents) LNil)))))))
      (body))
    (rule c-creator_source
      (head (metta-equation (tptp-compact:v ?policy (bnf creator_source ?name ?info ?parents))
                            (tptp-compact:expression (LCons creator (LCons (tptp-compact:v ?policy ?name) (LCons (tptp-compact:info ?policy ?info) (LCons (tptp-compact:parents ?policy ?parents) LNil)))))))
      (body))
    (rule c-file_source
      (head (metta-equation (tptp-compact:v ?policy (bnf file_source ?file ?info))
                            (tptp-compact:expression (LCons file (LCons (tptp-compact:file-name ?file) (tptp-compact:v ?policy ?info))))))
      (body))
    (rule c-file_info (head (metta-equation (tptp-compact:v ?policy (bnf file_info ?name)) (LCons (tptp-compact:v ?policy ?name) LNil))) (body))
    (rule c-theory
      (head (metta-equation (tptp-compact:v ?policy (bnf theory ?name ?info))
                            (tptp-compact:expression (LCons theory (LCons (tptp-compact:v ?policy ?name) (tptp-compact:v ?policy ?info))))))
      (body))
    ; Useful info and parents always hold lists: [] is the empty list there.
    (rule info-empty (head (metta-equation (tptp-compact:info ?policy "[]") (tptp-compact:list-value LNil))) (body))
    (rule info-terms (head (metta-equation (tptp-compact:info ?policy (bnf general_list ?terms)) (tptp-compact:v ?policy ?terms))) (body))
    (rule parents-empty (head (metta-equation (tptp-compact:parents ?policy "[]") (tptp-compact:list-value LNil))) (body))
    (rule parents-list (head (metta-equation (tptp-compact:parents ?policy (bnf parents ?list)) (tptp-compact:v ?policy (bnf parents ?list)))) (body))
    (rule c-parents (head (metta-equation (tptp-compact:v ?policy (bnf parents ?list)) (tptp-compact:v ?policy ?list))) (body))
    (rule c-parent_list (head (metta-equation (tptp-compact:v ?policy (bnf parent_list ?first ?rest)) (tptp-compact:items ?policy ?first ?rest))) (body))
    (rule c-comma_parent_info (head (metta-equation (tptp-compact:v ?policy (bnf comma_parent_info ?parent)) (tptp-compact:v ?policy ?parent))) (body))
    ; A parent with details is (: PARENT DETAILS).
    (rule c-parent_info
      (head (metta-equation (tptp-compact:v ?policy (bnf parent_info ?source ?details))
                            (tptp-compact:parent (tptp-compact:v ?policy ?source) (tptp-compact:v ?policy ?details))))
      (body))
    (rule parent-plain (head (metta-equation (tptp-compact:parent ?source LNil) ?source)) (body))
    (rule parent-detailed (head (metta-equation (tptp-compact:parent ?source (LCons ?details LNil)) (: ?source ?details))) (body))
    (rule c-parent_details (head (metta-equation (tptp-compact:v ?policy (bnf parent_details ?term)) (LCons (tptp-compact:v ?policy ?term) LNil))) (body))

    ; ------------------------------------------------------ general terms
    (rule c-general_term (head (metta-equation (tptp-compact:v ?policy (bnf general_term ?data ?term)) (tptp-compact:fixed : ?policy ?data ?term))) (body))
    (rule c-general_function (head (metta-equation (tptp-compact:v ?policy (bnf general_function ?word ?terms)) (tptp-compact:apply ?policy ?word ?terms))) (body))
    (rule c-general_list (head (metta-equation (tptp-compact:v ?policy (bnf general_list ?terms)) (tptp-compact:v ?policy ?terms))) (body))
    (rule c-general_terms (head (metta-equation (tptp-compact:v ?policy (bnf general_terms ?first ?rest)) (tptp-compact:items ?policy ?first ?rest))) (body))
    (rule c-comma_general_term (head (metta-equation (tptp-compact:v ?policy (bnf comma_general_term ?term)) (tptp-compact:v ?policy ?term))) (body))
    ; $thf(F) and its kin are the defined word applied to the formula.
    (rule c-formula_data/0 (head (metta-equation (tptp-compact:v ?policy (bnf formula_data/0 ?f)) (tptp-compact:embedded ?policy "thf" (tptp-compact:v ?policy ?f)))) (body))
    (rule c-formula_data/1 (head (metta-equation (tptp-compact:v ?policy (bnf formula_data/1 ?f)) (tptp-compact:embedded ?policy "tff" (tptp-compact:v ?policy ?f)))) (body))
    (rule c-formula_data/2 (head (metta-equation (tptp-compact:v ?policy (bnf formula_data/2 ?f)) (tptp-compact:embedded ?policy "fof" (tptp-compact:v ?policy ?f)))) (body))
    (rule c-formula_data/3 (head (metta-equation (tptp-compact:v ?policy (bnf formula_data/3 ?f)) (tptp-compact:embedded ?policy "cnf" (tptp-compact:clause ?policy ?f)))) (body))
    (rule c-formula_data/4 (head (metta-equation (tptp-compact:v ?policy (bnf formula_data/4 ?t)) (tptp-compact:embedded ?policy "fot" (tptp-compact:v ?policy ?t)))) (body))
    (rule embedded
      (head (metta-equation (tptp-compact:embedded ?policy ?word ?value)
                            (tptp-compact:expression (LCons (tptp-compact:defined ?policy ?word) (LCons ?value LNil)))))
      (body))

    ; ---------------------------------------------------------------- FOF
    (rule c-fof_binary_nonassoc (head (metta-equation (tptp-compact:v ?policy (bnf fof_binary_nonassoc ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-fof_or_formula/0 (head (metta-equation (tptp-compact:v ?policy (bnf fof_or_formula/0 ?a ?b)) (tptp-compact:fixed ∨ ?policy ?a ?b))) (body))
    (rule c-fof_or_formula/1 (head (metta-equation (tptp-compact:v ?policy (bnf fof_or_formula/1 ?chain ?b)) (tptp-compact:left-chain ?policy fof_or_formula/1 ?chain (LCons (tptp-compact:v ?policy ?b) LNil)))) (body))
    (rule c-fof_and_formula/0 (head (metta-equation (tptp-compact:v ?policy (bnf fof_and_formula/0 ?a ?b)) (tptp-compact:fixed ∧ ?policy ?a ?b))) (body))
    (rule c-fof_and_formula/1 (head (metta-equation (tptp-compact:v ?policy (bnf fof_and_formula/1 ?chain ?b)) (tptp-compact:left-chain ?policy fof_and_formula/1 ?chain (LCons (tptp-compact:v ?policy ?b) LNil)))) (body))
    (rule c-fof_unary_formula (head (metta-equation (tptp-compact:v ?policy (bnf fof_unary_formula ?c ?f)) (tptp-compact:unary ?policy ?c ?f))) (body))
    (rule c-fof_infix_unary (head (metta-equation (tptp-compact:v ?policy (bnf fof_infix_unary ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-fof_unitary_formula (head (metta-equation (tptp-compact:v ?policy (bnf fof_unitary_formula ?f)) (tptp-compact:v ?policy ?f))) (body))
    (rule c-fof_quantified_formula (head (metta-equation (tptp-compact:v ?policy (bnf fof_quantified_formula ?q ?vs ?f)) (tptp-compact:quantify ?policy ?q ?vs ?f))) (body))
    (rule c-fof_defined_infix_formula (head (metta-equation (tptp-compact:v ?policy (bnf fof_defined_infix_formula ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-fof_plain_term (head (metta-equation (tptp-compact:v ?policy (bnf fof_plain_term ?f ?args)) (tptp-compact:apply ?policy ?f ?args))) (body))
    (rule c-fof_defined_plain_term (head (metta-equation (tptp-compact:v ?policy (bnf fof_defined_plain_term ?f ?args)) (tptp-compact:apply ?policy ?f ?args))) (body))
    (rule c-fof_system_term (head (metta-equation (tptp-compact:v ?policy (bnf fof_system_term ?f ?args)) (tptp-compact:apply ?policy ?f ?args))) (body))
    (rule c-fof_sequent/0 (head (metta-equation (tptp-compact:v ?policy (bnf fof_sequent/0 ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-fof_sequent/1 (head (metta-equation (tptp-compact:v ?policy (bnf fof_sequent/1 ?s)) (tptp-compact:v ?policy ?s))) (body))
    (rule c-fof_formula_tuple (head (metta-equation (tptp-compact:v ?policy (bnf fof_formula_tuple ?list)) (tptp-compact:v ?policy ?list))) (body))
    (rule c-fof_formula_tuple_list (head (metta-equation (tptp-compact:v ?policy (bnf fof_formula_tuple_list ?first ?rest)) (tptp-compact:items ?policy ?first ?rest))) (body))
    (rule c-comma_fof_logic_formula (head (metta-equation (tptp-compact:v ?policy (bnf comma_fof_logic_formula ?f)) (tptp-compact:v ?policy ?f))) (body))

    ; ---------------------------------------------------------------- TFF
    (rule c-tff_binary_nonassoc (head (metta-equation (tptp-compact:v ?policy (bnf tff_binary_nonassoc ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-tff_or_formula/0 (head (metta-equation (tptp-compact:v ?policy (bnf tff_or_formula/0 ?a ?b)) (tptp-compact:fixed ∨ ?policy ?a ?b))) (body))
    (rule c-tff_or_formula/1 (head (metta-equation (tptp-compact:v ?policy (bnf tff_or_formula/1 ?chain ?b)) (tptp-compact:left-chain ?policy tff_or_formula/1 ?chain (LCons (tptp-compact:v ?policy ?b) LNil)))) (body))
    (rule c-tff_and_formula/0 (head (metta-equation (tptp-compact:v ?policy (bnf tff_and_formula/0 ?a ?b)) (tptp-compact:fixed ∧ ?policy ?a ?b))) (body))
    (rule c-tff_and_formula/1 (head (metta-equation (tptp-compact:v ?policy (bnf tff_and_formula/1 ?chain ?b)) (tptp-compact:left-chain ?policy tff_and_formula/1 ?chain (LCons (tptp-compact:v ?policy ?b) LNil)))) (body))
    (rule c-tff_unitary_formula (head (metta-equation (tptp-compact:v ?policy (bnf tff_unitary_formula ?f)) (tptp-compact:v ?policy ?f))) (body))
    (rule c-tff_quantified_formula (head (metta-equation (tptp-compact:v ?policy (bnf tff_quantified_formula ?q ?vs ?f)) (tptp-compact:quantify ?policy ?q ?vs ?f))) (body))
    (rule c-tff_typed_variable (head (metta-equation (tptp-compact:v ?policy (bnf tff_typed_variable ?x ?t)) (tptp-compact:fixed : ?policy ?x ?t))) (body))
    (rule c-tff_prefix_unary (head (metta-equation (tptp-compact:v ?policy (bnf tff_prefix_unary ?c ?f)) (tptp-compact:unary ?policy ?c ?f))) (body))
    (rule c-tff_infix_unary (head (metta-equation (tptp-compact:v ?policy (bnf tff_infix_unary ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-tff_plain_atomic (head (metta-equation (tptp-compact:v ?policy (bnf tff_plain_atomic ?f ?args)) (tptp-compact:apply ?policy ?f ?args))) (body))
    (rule c-tff_defined_plain (head (metta-equation (tptp-compact:v ?policy (bnf tff_defined_plain ?f ?args)) (tptp-compact:apply ?policy ?f ?args))) (body))
    (rule c-tff_defined_infix (head (metta-equation (tptp-compact:v ?policy (bnf tff_defined_infix ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-tff_system_atomic (head (metta-equation (tptp-compact:v ?policy (bnf tff_system_atomic ?f ?args)) (tptp-compact:apply ?policy ?f ?args))) (body))
    (rule c-txf_let
      (head (metta-equation (tptp-compact:v ?policy (bnf txf_let ?types ?defns ?body))
                            (tptp-compact:let ?policy (tptp-compact:typings ?policy ?types) (tptp-compact:definitions ?policy ?defns) (tptp-compact:v ?policy ?body))))
      (body))
    (rule c-txf_let_types (head (metta-equation (tptp-compact:v ?policy (bnf txf_let_types ?typings)) (tptp-compact:v ?policy ?typings))) (body))
    (rule c-txf_let_defns (head (metta-equation (tptp-compact:v ?policy (bnf txf_let_defns ?defns)) (tptp-compact:v ?policy ?defns))) (body))
    (rule c-txf_let_defn (head (metta-equation (tptp-compact:v ?policy (bnf txf_let_defn ?lhs ?c ?rhs)) (tptp-compact:operator ?policy ?c ?lhs ?rhs))) (body))
    (rule c-nxf_atom (head (metta-equation (tptp-compact:v ?policy (bnf nxf_atom ?c ?args)) (tptp-compact:apply ?policy ?c ?args))) (body))
    (rule c-tff_unitary_term (head (metta-equation (tptp-compact:v ?policy (bnf tff_unitary_term ?f)) (tptp-compact:v ?policy ?f))) (body))
    (rule c-txf_tuple (head (metta-equation (tptp-compact:v ?policy (bnf txf_tuple ?args)) (tptp-compact:v ?policy ?args))) (body))
    (rule c-tff_arguments (head (metta-equation (tptp-compact:v ?policy (bnf tff_arguments ?first ?rest)) (tptp-compact:items ?policy ?first ?rest))) (body))
    (rule c-comma_tff_term (head (metta-equation (tptp-compact:v ?policy (bnf comma_tff_term ?t)) (tptp-compact:v ?policy ?t))) (body))
    (rule c-tff_atom_typing/0 (head (metta-equation (tptp-compact:v ?policy (bnf tff_atom_typing/0 ?a ?t)) (tptp-compact:fixed : ?policy ?a ?t))) (body))
    (rule c-tff_atom_typing/1 (head (metta-equation (tptp-compact:v ?policy (bnf tff_atom_typing/1 ?t)) (tptp-compact:v ?policy ?t))) (body))
    (rule c-tff_non_atomic_type (head (metta-equation (tptp-compact:v ?policy (bnf tff_non_atomic_type ?t)) (tptp-compact:v ?policy ?t))) (body))
    (rule c-tf1_quantified_type (head (metta-equation (tptp-compact:v ?policy (bnf tf1_quantified_type ?q ?vs ?t)) (tptp-compact:quantify ?policy ?q ?vs ?t))) (body))
    (rule c-tff_monotype (head (metta-equation (tptp-compact:v ?policy (bnf tff_monotype ?t)) (tptp-compact:v ?policy ?t))) (body))
    (rule c-tff_unitary_type (head (metta-equation (tptp-compact:v ?policy (bnf tff_unitary_type ?t)) (tptp-compact:v ?policy ?t))) (body))
    (rule c-tff_atomic_type/3 (head (metta-equation (tptp-compact:v ?policy (bnf tff_atomic_type/3 ?f ?args)) (tptp-compact:apply ?policy ?f ?args))) (body))
    (rule c-tff_atomic_type/4 (head (metta-equation (tptp-compact:v ?policy (bnf tff_atomic_type/4 ?t)) (tptp-compact:v ?policy ?t))) (body))
    (rule c-tff_mapping_type (head (metta-equation (tptp-compact:v ?policy (bnf tff_mapping_type ?a ?b)) (tptp-compact:fixed ⇒ ?policy ?a ?b))) (body))
    (rule c-tff_xprod_type/0 (head (metta-equation (tptp-compact:v ?policy (bnf tff_xprod_type/0 ?a ?b)) (tptp-compact:fixed × ?policy ?a ?b))) (body))
    (rule c-tff_xprod_type/1 (head (metta-equation (tptp-compact:v ?policy (bnf tff_xprod_type/1 ?chain ?b)) (tptp-compact:left-chain ?policy tff_xprod_type/1 ?chain (LCons (tptp-compact:v ?policy ?b) LNil)))) (body))
    (rule c-txf_tuple_type (head (metta-equation (tptp-compact:v ?policy (bnf txf_tuple_type ?types)) (tptp-compact:v ?policy ?types))) (body))
    (rule c-tff_subtype (head (metta-equation (tptp-compact:v ?policy (bnf tff_subtype ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-txf_definition (head (metta-equation (tptp-compact:v ?policy (bnf txf_definition ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-txf_sequent (head (metta-equation (tptp-compact:v ?policy (bnf txf_sequent ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    ; A long connective is its name with its parameters, (§box) for {$box} and
    ; (§box P...) for {$box(P...)}, so it differs from the defined word $box.
    (rule c-nhf_long_connective/0 (head (metta-equation (tptp-compact:v ?policy (bnf nhf_long_connective/0 ?name)) (tptp-compact:expression (LCons (tptp-compact:v ?policy ?name) LNil)))) (body))
    (rule c-nhf_long_connective/1 (head (metta-equation (tptp-compact:v ?policy (bnf nhf_long_connective/1 ?name ?params)) (tptp-compact:apply ?policy ?name ?params))) (body))
    (rule c-nxf_long_connective/0 (head (metta-equation (tptp-compact:v ?policy (bnf nxf_long_connective/0 ?name)) (tptp-compact:expression (LCons (tptp-compact:v ?policy ?name) LNil)))) (body))
    (rule c-nxf_long_connective/1 (head (metta-equation (tptp-compact:v ?policy (bnf nxf_long_connective/1 ?name ?params)) (tptp-compact:apply ?policy ?name ?params))) (body))
    (rule c-ntf_index
      (head (metta-equation (tptp-compact:v ?policy (bnf ntf_index ?t))
                            (tptp-compact:expression (LCons (tptp-compact:defined ?policy "#") (LCons (tptp-compact:v ?policy ?t) LNil)))))
      (body))

    ; ---------------------------------------------------------------- THF
    (rule c-thf_binary_nonassoc (head (metta-equation (tptp-compact:v ?policy (bnf thf_binary_nonassoc ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-thf_or_formula/0 (head (metta-equation (tptp-compact:v ?policy (bnf thf_or_formula/0 ?a ?b)) (tptp-compact:fixed ∨ ?policy ?a ?b))) (body))
    (rule c-thf_or_formula/1 (head (metta-equation (tptp-compact:v ?policy (bnf thf_or_formula/1 ?chain ?b)) (tptp-compact:left-chain ?policy thf_or_formula/1 ?chain (LCons (tptp-compact:v ?policy ?b) LNil)))) (body))
    (rule c-thf_and_formula/0 (head (metta-equation (tptp-compact:v ?policy (bnf thf_and_formula/0 ?a ?b)) (tptp-compact:fixed ∧ ?policy ?a ?b))) (body))
    (rule c-thf_and_formula/1 (head (metta-equation (tptp-compact:v ?policy (bnf thf_and_formula/1 ?chain ?b)) (tptp-compact:left-chain ?policy thf_and_formula/1 ?chain (LCons (tptp-compact:v ?policy ?b) LNil)))) (body))
    ; An application in function position extends the application.
    (rule c-thf_apply_formula/0
      (head (metta-equation (tptp-compact:v ?policy (bnf thf_apply_formula/0 ?f ?a))
                            (tptp-compact:expression (tptp-compact:append (tptp-compact:spine ?policy ?f) (LCons (tptp-compact:v ?policy ?a) LNil)))))
      (body))
    (rule c-thf_apply_formula/1 (head (metta-equation (tptp-compact:v ?policy (bnf thf_apply_formula/1 ?chain ?a)) (tptp-compact:left-chain ?policy thf_apply_formula/1 ?chain (LCons (tptp-compact:v ?policy ?a) LNil)))) (body))
    (rule spine
      (head (metta-equation (tptp-compact:spine ?policy ?f)
                            (tptp-compact:spine-at ?policy (langdef:ground-equal (tptp-compact:kind ?f) thf_unitary_formula) ?f)))
      (body))
    (rule spine-parenthesized (head (metta-equation (tptp-compact:spine-at ?policy LangDef:SameValue (bnf thf_unitary_formula ?inner)) (tptp-compact:spine ?policy ?inner))) (body))
    (rule spine-unparenthesized
      (head (metta-equation (tptp-compact:spine-at ?policy LangDef:DifferentValue ?f)
                            (tptp-compact:spine-of ?policy
                              (tptp-compact:one-of (tptp-compact:kind ?f) (LCons thf_apply_formula/0 (LCons thf_apply_formula/1 LNil)))
                              ?f)))
      (body))
    (rule spine-application (head (metta-equation (tptp-compact:spine-of ?policy LangDef:SameValue ?f) (langdef:expression->list (tptp-compact:v ?policy ?f)))) (body))
    (rule spine-head (head (metta-equation (tptp-compact:spine-of ?policy LangDef:DifferentValue ?f) (LCons (tptp-compact:v ?policy ?f) LNil))) (body))
    (rule c-thf_unitary_formula (head (metta-equation (tptp-compact:v ?policy (bnf thf_unitary_formula ?f)) (tptp-compact:v ?policy ?f))) (body))
    (rule c-thf_quantified_formula
      (head (metta-equation (tptp-compact:v ?policy (bnf thf_quantified_formula ?quantification ?f))
                            (tptp-compact:expression (tptp-compact:append (tptp-compact:v ?policy ?quantification) (LCons (tptp-compact:v ?policy ?f) LNil)))))
      (body))
    (rule c-thf_quantification
      (head (metta-equation (tptp-compact:v ?policy (bnf thf_quantification ?q ?vs))
                            (LCons (tptp-compact:v ?policy ?q) (LCons (tptp-compact:v ?policy ?vs) LNil))))
      (body))
    (rule c-thf_typed_variable (head (metta-equation (tptp-compact:v ?policy (bnf thf_typed_variable ?x ?t)) (tptp-compact:fixed : ?policy ?x ?t))) (body))
    (rule c-thf_prefix_unary (head (metta-equation (tptp-compact:v ?policy (bnf thf_prefix_unary ?c ?f)) (tptp-compact:unary ?policy ?c ?f))) (body))
    (rule c-thf_infix_unary (head (metta-equation (tptp-compact:v ?policy (bnf thf_infix_unary ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-thf_defined_atomic (head (metta-equation (tptp-compact:v ?policy (bnf thf_defined_atomic ?c)) (tptp-compact:v ?policy ?c))) (body))
    (rule c-thf_defined_infix (head (metta-equation (tptp-compact:v ?policy (bnf thf_defined_infix ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-thf_let
      (head (metta-equation (tptp-compact:v ?policy (bnf thf_let ?types ?defns ?body))
                            (tptp-compact:let ?policy (tptp-compact:typings ?policy ?types) (tptp-compact:definitions ?policy ?defns) (tptp-compact:v ?policy ?body))))
      (body))
    (rule c-thf_let_types (head (metta-equation (tptp-compact:v ?policy (bnf thf_let_types ?typings)) (tptp-compact:v ?policy ?typings))) (body))
    (rule c-thf_let_defns (head (metta-equation (tptp-compact:v ?policy (bnf thf_let_defns ?defns)) (tptp-compact:v ?policy ?defns))) (body))
    (rule c-thf_let_defn (head (metta-equation (tptp-compact:v ?policy (bnf thf_let_defn ?lhs ?c ?rhs)) (tptp-compact:operator ?policy ?c ?lhs ?rhs))) (body))
    (rule c-thf_unitary_term (head (metta-equation (tptp-compact:v ?policy (bnf thf_unitary_term ?f)) (tptp-compact:v ?policy ?f))) (body))
    (rule c-thf_tuple (head (metta-equation (tptp-compact:v ?policy (bnf thf_tuple ?list)) (tptp-compact:v ?policy ?list))) (body))
    (rule c-thf_fof_function/0 (head (metta-equation (tptp-compact:v ?policy (bnf thf_fof_function/0 ?f ?args)) (tptp-compact:apply ?policy ?f ?args))) (body))
    (rule c-thf_fof_function/1 (head (metta-equation (tptp-compact:v ?policy (bnf thf_fof_function/1 ?f ?args)) (tptp-compact:apply ?policy ?f ?args))) (body))
    (rule c-thf_fof_function/2 (head (metta-equation (tptp-compact:v ?policy (bnf thf_fof_function/2 ?f ?args)) (tptp-compact:apply ?policy ?f ?args))) (body))
    (rule c-thf_formula_list (head (metta-equation (tptp-compact:v ?policy (bnf thf_formula_list ?first ?rest)) (tptp-compact:items ?policy ?first ?rest))) (body))
    (rule c-comma_thf_logic_formula (head (metta-equation (tptp-compact:v ?policy (bnf comma_thf_logic_formula ?f)) (tptp-compact:v ?policy ?f))) (body))
    (rule c-thf_atom_typing/0 (head (metta-equation (tptp-compact:v ?policy (bnf thf_atom_typing/0 ?a ?t)) (tptp-compact:fixed : ?policy ?a ?t))) (body))
    (rule c-thf_atom_typing/1 (head (metta-equation (tptp-compact:v ?policy (bnf thf_atom_typing/1 ?t)) (tptp-compact:v ?policy ?t))) (body))
    (rule c-thf_mapping_type/0 (head (metta-equation (tptp-compact:v ?policy (bnf thf_mapping_type/0 ?a ?b)) (tptp-compact:fixed ⇒ ?policy ?a ?b))) (body))
    (rule c-thf_mapping_type/1 (head (metta-equation (tptp-compact:v ?policy (bnf thf_mapping_type/1 ?a ?b)) (tptp-compact:fixed ⇒ ?policy ?a ?b))) (body))
    (rule c-thf_xprod_type/0 (head (metta-equation (tptp-compact:v ?policy (bnf thf_xprod_type/0 ?a ?b)) (tptp-compact:fixed × ?policy ?a ?b))) (body))
    (rule c-thf_xprod_type/1 (head (metta-equation (tptp-compact:v ?policy (bnf thf_xprod_type/1 ?chain ?b)) (tptp-compact:left-chain ?policy thf_xprod_type/1 ?chain (LCons (tptp-compact:v ?policy ?b) LNil)))) (body))
    (rule c-thf_union_type/0 (head (metta-equation (tptp-compact:v ?policy (bnf thf_union_type/0 ?a ?b)) (tptp-compact:fixed ⊎ ?policy ?a ?b))) (body))
    (rule c-thf_union_type/1 (head (metta-equation (tptp-compact:v ?policy (bnf thf_union_type/1 ?chain ?b)) (tptp-compact:left-chain ?policy thf_union_type/1 ?chain (LCons (tptp-compact:v ?policy ?b) LNil)))) (body))
    (rule c-thf_subtype (head (metta-equation (tptp-compact:v ?policy (bnf thf_subtype ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-thf_definition (head (metta-equation (tptp-compact:v ?policy (bnf thf_definition ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))
    (rule c-thf_sequent (head (metta-equation (tptp-compact:v ?policy (bnf thf_sequent ?a ?c ?b)) (tptp-compact:operator ?policy ?c ?a ?b))) (body))

    ; --------------------------------------------------------------- $let
    (rule let
      (head (metta-equation (tptp-compact:let ?policy ?typings ?definitions ?body)
                            (tptp-compact:expression (LCons (tptp-compact:defined ?policy "let") (LCons ?typings (LCons ?definitions (LCons ?body LNil)))))))
      (body))
    (rule typings (head (metta-equation (tptp-compact:typings ?policy ?types) (tptp-compact:typings-by ?policy (tptp-compact:kind ?types) ?types))) (body))
    (rule typings-thf (head (metta-equation (tptp-compact:typings-by ?policy thf_let_types ?types) (tptp-compact:v ?policy ?types))) (body))
    (rule typings-thf-one (head (metta-equation (tptp-compact:typings-by ?policy thf_atom_typing/0 ?typing) (tptp-compact:single (tptp-compact:v ?policy ?typing)))) (body))
    (rule typings-thf-one-parenthesized (head (metta-equation (tptp-compact:typings-by ?policy thf_atom_typing/1 ?typing) (tptp-compact:single (tptp-compact:v ?policy ?typing)))) (body))
    (rule typings-txf (head (metta-equation (tptp-compact:typings-by ?policy txf_let_types ?types) (tptp-compact:v ?policy ?types))) (body))
    (rule typings-txf-one (head (metta-equation (tptp-compact:typings-by ?policy tff_atom_typing/0 ?typing) (tptp-compact:single (tptp-compact:v ?policy ?typing)))) (body))
    (rule typings-txf-one-parenthesized (head (metta-equation (tptp-compact:typings-by ?policy tff_atom_typing/1 ?typing) (tptp-compact:single (tptp-compact:v ?policy ?typing)))) (body))
    (rule definitions (head (metta-equation (tptp-compact:definitions ?policy ?defns) (tptp-compact:definitions-by ?policy (tptp-compact:kind ?defns) ?defns))) (body))
    (rule definitions-thf (head (metta-equation (tptp-compact:definitions-by ?policy thf_let_defns ?defns) (tptp-compact:v ?policy ?defns))) (body))
    (rule definitions-thf-one (head (metta-equation (tptp-compact:definitions-by ?policy thf_let_defn ?defn) (tptp-compact:single (tptp-compact:v ?policy ?defn)))) (body))
    (rule definitions-txf (head (metta-equation (tptp-compact:definitions-by ?policy txf_let_defns ?defns) (tptp-compact:v ?policy ?defns))) (body))
    (rule definitions-txf-one (head (metta-equation (tptp-compact:definitions-by ?policy txf_let_defn ?defn) (tptp-compact:single (tptp-compact:v ?policy ?defn)))) (body))

    ; ----------------------------------------------------------- literals
    (rule l-empty (head (metta-equation (tptp-compact:v ?policy "[]") (tptp-compact:list-value LNil))) (body))
    (rule l-forall (head (metta-equation (tptp-compact:v ?policy "!") ∀)) (body))
    (rule l-exists (head (metta-equation (tptp-compact:v ?policy "?") ∃)) (body))
    (rule l-lambda (head (metta-equation (tptp-compact:v ?policy "^") λ)) (body))
    (rule l-choice (head (metta-equation (tptp-compact:v ?policy "@+") ε)) (body))
    (rule l-description (head (metta-equation (tptp-compact:v ?policy "@-") ι)) (body))
    (rule l-type-forall (head (metta-equation (tptp-compact:v ?policy "!>") Π)) (body))
    (rule l-type-exists (head (metta-equation (tptp-compact:v ?policy "?*") Σ)) (body))
    (rule l-hash (head (metta-equation (tptp-compact:v ?policy "#") (tptp-compact:defined ?policy "#"))) (body))
    (rule l-not (head (metta-equation (tptp-compact:v ?policy "~") ¬)) (body))
    (rule l-or (head (metta-equation (tptp-compact:v ?policy "|") ∨)) (body))
    (rule l-and (head (metta-equation (tptp-compact:v ?policy "&") ∧)) (body))
    (rule l-iff (head (metta-equation (tptp-compact:v ?policy "<=>") ↔)) (body))
    (rule l-implies (head (metta-equation (tptp-compact:v ?policy "=>") →)) (body))
    (rule l-implied (head (metta-equation (tptp-compact:v ?policy "<=") ←)) (body))
    (rule l-xor (head (metta-equation (tptp-compact:v ?policy "<~>") ⊕)) (body))
    (rule l-nor (head (metta-equation (tptp-compact:v ?policy "~|") ⊽)) (body))
    (rule l-nand (head (metta-equation (tptp-compact:v ?policy "~&") ⊼)) (body))
    (rule l-equal (head (metta-equation (tptp-compact:v ?policy "=") ≈)) (body))
    (rule l-unequal (head (metta-equation (tptp-compact:v ?policy "!=") ≉)) (body))
    (rule l-sequent (head (metta-equation (tptp-compact:v ?policy "-->") ⊢)) (body))
    (rule l-assign (head (metta-equation (tptp-compact:v ?policy ":=") ≔)) (body))
    (rule l-identical (head (metta-equation (tptp-compact:v ?policy "==") ≡)) (body))
    (rule l-subtype (head (metta-equation (tptp-compact:v ?policy "<<") ⊑)) (body))
    (rule l-box (head (metta-equation (tptp-compact:v ?policy "[.]") □)) (body))
    (rule l-diamond (head (metta-equation (tptp-compact:v ?policy "<.>") ◇)) (body))
    (rule l-braces (head (metta-equation (tptp-compact:v ?policy "{.}") (tptp-compact:defined ?policy "{.}"))) (body))
    (rule l-parentheses (head (metta-equation (tptp-compact:v ?policy "(.)") (tptp-compact:defined ?policy "(.)"))) (body))
    (rule l-pi (head (metta-equation (tptp-compact:v ?policy "!!") (tptp-compact:defined ?policy "!!"))) (body))
    (rule l-sigma (head (metta-equation (tptp-compact:v ?policy "??") (tptp-compact:defined ?policy "??"))) (body))
    (rule l-choice-term (head (metta-equation (tptp-compact:v ?policy "@@+") (tptp-compact:defined ?policy "@@+"))) (body))
    (rule l-description-term (head (metta-equation (tptp-compact:v ?policy "@@-") (tptp-compact:defined ?policy "@@-"))) (body))
    (rule l-equal-term (head (metta-equation (tptp-compact:v ?policy "@=") (tptp-compact:defined ?policy "@="))) (body))
    (rule l-all (head (metta-equation (tptp-compact:v ?policy "*") *)) (body))
    (rule l-unknown (head (metta-equation (tptp-compact:v ?policy "unknown") unknown)) (body))

    ; ------------------------------------------------------ tagged leaves
    (rule t-lower_word (head (metta-equation (tptp-compact:v tagged (bnf lower_word ?text)) (word ?text))) (body))
    (rule t-single_quoted (head (metta-equation (tptp-compact:v tagged (bnf single_quoted ?text)) (word (tptp-compact:unquote ?text)))) (body))
    (rule t-back_quoted (head (metta-equation (tptp-compact:v tagged (bnf back_quoted ?text)) (word ?text))) (body))
    (rule t-upper_word (head (metta-equation (tptp-compact:v tagged (bnf upper_word ?text)) (var ?text))) (body))
    (rule t-dollar_word (head (metta-equation (tptp-compact:v tagged (bnf dollar_word ?text)) (defined (tptp-compact:after-sigil 1 ?text)))) (body))
    (rule t-dollar_dollar_word (head (metta-equation (tptp-compact:v tagged (bnf dollar_dollar_word ?text)) (system (tptp-compact:after-sigil 2 ?text)))) (body))
    (rule t-distinct_object (head (metta-equation (tptp-compact:v tagged (bnf distinct_object ?text)) (distinct (tptp-compact:unquote ?text)))) (body))
    (rule t-integer (head (metta-equation (tptp-compact:v tagged (bnf integer ?text)) (langdef:decimal-integer->number ?text))) (body))
    (rule t-rational (head (metta-equation (tptp-compact:v tagged (bnf rational ?text)) (tptp-compact:rational (langdef:decimal-rational->value ?text)))) (body))
    (rule t-real (head (metta-equation (tptp-compact:v tagged (bnf real ?text)) (real (langdef:decimal-real->canonical ?text)))) (body))
    (rule rational (head (metta-equation (tptp-compact:rational (LCons ?numerator (LCons ?denominator LNil))) (rat ?numerator ?denominator))) (body))
    (rule defined-tagged (head (metta-equation (tptp-compact:defined tagged ?word) (defined ?word))) (body))
    (rule system-tagged (head (metta-equation (tptp-compact:system tagged ?word) (system ?word))) (body))

    ; ------------------------------------------------------- mixed leaves
    ; A leaf is bare where the host reads the bare spelling back as the same
    ; leaf: a word spelled like a lower word that is not reserved, a variable
    ; that is not a host literal (those take ?), $w and $$w as §w and §§w when
    ; w is alphanumeric, a distinct object as a string, a real as the nearest
    ; host float when one keeps its value.  Any other leaf keeps its tagged
    ; form, so a bare atom always decodes to one leaf.
    (rule m-lower_word (head (metta-equation (tptp-compact:v mixed (bnf lower_word ?text)) (tptp-compact:word mixed ?text))) (body))
    (rule m-single_quoted (head (metta-equation (tptp-compact:v mixed (bnf single_quoted ?text)) (tptp-compact:quoted-word mixed (tptp-compact:unquote ?text)))) (body))
    (rule m-back_quoted (head (metta-equation (tptp-compact:v mixed (bnf back_quoted ?text)) (word ?text))) (body))
    (rule m-upper_word (head (metta-equation (tptp-compact:v mixed (bnf upper_word ?text)) (tptp-compact:variable mixed ?text))) (body))
    (rule m-dollar_word (head (metta-equation (tptp-compact:v mixed (bnf dollar_word ?text)) (tptp-compact:defined mixed (tptp-compact:after-sigil 1 ?text)))) (body))
    (rule m-dollar_dollar_word (head (metta-equation (tptp-compact:v mixed (bnf dollar_dollar_word ?text)) (tptp-compact:system mixed (tptp-compact:after-sigil 2 ?text)))) (body))
    (rule m-distinct_object (head (metta-equation (tptp-compact:v mixed (bnf distinct_object ?text)) (tptp-compact:unquote ?text))) (body))
    (rule m-integer (head (metta-equation (tptp-compact:v mixed (bnf integer ?text)) (langdef:decimal-integer->number ?text))) (body))
    (rule m-rational (head (metta-equation (tptp-compact:v mixed (bnf rational ?text)) (tptp-compact:rational (langdef:decimal-rational->value ?text)))) (body))
    (rule m-real (head (metta-equation (tptp-compact:v mixed (bnf real ?text)) (tptp-compact:float-or-exact (langdef:decimal-real->float ?text) ?text))) (body))

    ; ------------------------------------------------------ native leaves
    ; Opt-in and lossy: words and variables are bare whenever their spelling is
    ; a symbol, so host literals and reserved spellings read back as the host
    ; values and structure they spell.  Everything else is as in mixed.
    (rule n-lower_word (head (metta-equation (tptp-compact:v native (bnf lower_word ?text)) (tptp-compact:word native ?text))) (body))
    (rule n-single_quoted (head (metta-equation (tptp-compact:v native (bnf single_quoted ?text)) (tptp-compact:quoted-word native (tptp-compact:unquote ?text)))) (body))
    (rule n-back_quoted (head (metta-equation (tptp-compact:v native (bnf back_quoted ?text)) (word ?text))) (body))
    (rule n-upper_word (head (metta-equation (tptp-compact:v native (bnf upper_word ?text)) (tptp-compact:variable native ?text))) (body))
    (rule n-dollar_word (head (metta-equation (tptp-compact:v native (bnf dollar_word ?text)) (tptp-compact:defined native (tptp-compact:after-sigil 1 ?text)))) (body))
    (rule n-dollar_dollar_word (head (metta-equation (tptp-compact:v native (bnf dollar_dollar_word ?text)) (tptp-compact:system native (tptp-compact:after-sigil 2 ?text)))) (body))
    (rule n-distinct_object (head (metta-equation (tptp-compact:v native (bnf distinct_object ?text)) (tptp-compact:unquote ?text))) (body))
    (rule n-integer (head (metta-equation (tptp-compact:v native (bnf integer ?text)) (langdef:decimal-integer->number ?text))) (body))
    (rule n-rational (head (metta-equation (tptp-compact:v native (bnf rational ?text)) (tptp-compact:rational (langdef:decimal-rational->value ?text)))) (body))
    (rule n-real (head (metta-equation (tptp-compact:v native (bnf real ?text)) (tptp-compact:float-or-exact (langdef:decimal-real->float ?text) ?text))) (body))

    ; -------------------------------------------------------- leaf helpers
    ; The spellings a bare word must not have: the compact vocabulary and the
    ; words some host reads as literals.  Each set is built once per run.
    (rule reserved-words
      (head (metta-equation (tptp-compact:reserved-words)
                            (str:set (LCons "fof" (LCons "cnf" (LCons "tff" (LCons "thf" (LCons "tcf" (LCons "tpi" (LCons "incl"
                            (LCons "inference" (LCons "introduced" (LCons "file" (LCons "theory" (LCons "creator" (LCons "unknown"
                            (LCons "word" (LCons "var" (LCons "defined" (LCons "system" (LCons "distinct" (LCons "rat" (LCons "real"
                            (LCons "true" (LCons "false" (LCons "empty" LNil))))))))))))))))))))))))))
      (body))
    ; The variable spellings some host reads as literals.
    (rule host-literal-variables
      (head (metta-equation (tptp-compact:host-literal-variables)
                            (str:set (LCons "True" (LCons "False" (LCons "PI" (LCons "EXP" LNil)))))))
      (body))
    (rule word-mixed (head (metta-equation (tptp-compact:word mixed ?text) (tptp-compact:bare-or-word (str:set-member? (tptp-compact:reserved-words) ?text) ?text))) (body))
    (rule word-native (head (metta-equation (tptp-compact:word native ?text) (tptp-compact:symbol ?text))) (body))
    (rule bare-word (head (metta-equation (tptp-compact:bare-or-word False ?text) (tptp-compact:symbol ?text))) (body))
    (rule reserved-word (head (metta-equation (tptp-compact:bare-or-word True ?text) (word ?text))) (body))
    (rule quoted-word
      (head (metta-equation (tptp-compact:quoted-word ?policy ?text)
                            (tptp-compact:quoted-at ?policy (str:lower-word? ?text) ?text)))
      (body))
    (rule quoted-word-spelling (head (metta-equation (tptp-compact:quoted-at ?policy True ?text) (tptp-compact:word ?policy ?text))) (body))
    (rule quoted-other-spelling (head (metta-equation (tptp-compact:quoted-at ?policy False ?text) (word ?text))) (body))
    (rule variable-mixed
      (head (metta-equation (tptp-compact:variable mixed ?text)
                            (tptp-compact:variable-at (str:set-member? (tptp-compact:host-literal-variables) ?text) ?text)))
      (body))
    (rule variable-native (head (metta-equation (tptp-compact:variable native ?text) (tptp-compact:symbol ?text))) (body))
    (rule variable-bare (head (metta-equation (tptp-compact:variable-at False ?text) (tptp-compact:symbol ?text))) (body))
    (rule variable-escaped
      (head (metta-equation (tptp-compact:variable-at True ?text)
                            (str:string->symbol (str:concat "?" ?text))))
      (body))
    (rule defined-mixed (head (metta-equation (tptp-compact:defined mixed ?word) (tptp-compact:sigil defined "§" ?word))) (body))
    (rule defined-native (head (metta-equation (tptp-compact:defined native ?word) (tptp-compact:sigil defined "§" ?word))) (body))
    (rule system-mixed (head (metta-equation (tptp-compact:system mixed ?word) (tptp-compact:sigil system "§§" ?word))) (body))
    (rule system-native (head (metta-equation (tptp-compact:system native ?word) (tptp-compact:sigil system "§§" ?word))) (body))
    (rule sigil
      (head (metta-equation (tptp-compact:sigil ?category ?sigil ?word)
                            (tptp-compact:sigil-at (str:all-in? word ?word) ?category ?sigil ?word)))
      (body))
    (rule sigil-bare
      (head (metta-equation (tptp-compact:sigil-at True ?category ?sigil ?word)
                            (str:string->symbol (str:concat ?sigil ?word))))
      (body))
    (rule sigil-tagged
      (head (metta-equation (tptp-compact:sigil-at False ?category ?sigil ?word)
                            (tptp-compact:expression (LCons ?category (LCons ?word LNil)))))
      (body))
    (rule symbol (head (metta-equation (tptp-compact:symbol ?text) (str:string->symbol ?text))) (body))
    (rule float (head (metta-equation (tptp-compact:float-or-exact (LangDef:Float ?value) ?text) ?value)) (body))
    (rule no-float (head (metta-equation (tptp-compact:float-or-exact LangDef:NoFloat ?text) (real (langdef:decimal-real->canonical ?text)))) (body))

    ; ------------------------------------------------------------ spelling
    ; 'a\'b' is a'b and "a\"b" is a"b: the quotes go, each escaped scalar stays.
    (rule unquote
      (head (metta-equation (tptp-compact:unquote ?text)
                            (str:unescape tptp-single
                              (str:byte-slice ?text 1 (+ (str:byte-length ?text) -1)))))
      (body))
    ; $word and $$word name the word after the sigil.
    (rule after-sigil
      (head (metta-equation (tptp-compact:after-sigil ?count ?text)
                            (str:byte-slice ?text ?count (str:byte-length ?text))))
      (body))
  ))
