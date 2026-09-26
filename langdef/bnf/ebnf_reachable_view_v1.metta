; Entry-point restriction for an already declaration-validated EBNF document.
; Uses the existing occurrence-preserving declaration view for dependencies;
; filters the original definitions without rewriting their RHSs or reordering
; them. This preparation pass is not an input parser or a validation bypass.
; Requires ebnf_declaration_view_v1.metta. Name lookup is a linear scan; this
; reference transformation makes no claim of indexed preparation complexity.
; The search spends one element of a list of every name it could meet, the
; start and each reference of each rule, for each name it visits first, so
; it ends: the visited names are distinct and each is on that list.
(gslt-presentation-v1 EbnfReachableViewV1
  (signature
    (operator metta-equation 2)
    (operator langdef:ground-equal 2)
    (operator ebnf-v1:declaration-view 1)
    (operator ebnf-reach-v1:select 2)
    (operator ebnf-reach-v1:prepare 3)
    (operator ebnf-reach-v1:finish 2)
    (operator ebnf-reach-v1:visit 4)
    (operator ebnf-reach-v1:visit-next 5)
    (operator ebnf-reach-v1:names 2)
    (operator ebnf-reach-v1:alternative-names 2)
    (operator ebnf-reach-v1:element-names 2)
    (operator ebnf-reach-v1:names-exhausted 1)
    (operator ebnf-reach-v1:member 2)
    (operator ebnf-reach-v1:member-next 3)
    (operator ebnf-reach-v1:dependencies 3)
    (operator ebnf-reach-v1:dependencies-next 6)
    (operator ebnf-reach-v1:references 2)
    (operator ebnf-reach-v1:filter 2)
    (operator ebnf-reach-v1:filter-next 4)
    (operator ebnf-reach-v1:nil 0)
    (operator ebnf-reach-v1:cons 2)
    (operator bnf-v1:document 2)
    (operator bnf-v1:entries-nil 0)
    (operator bnf-v1:entries-cons 2)
    (operator bnf-v1:rule 3)
    (operator bnf-v1:comment 2)
    (operator bnf-v1:blank 1)
    (operator bnf-v1:expression 2)
    (operator bnf-v1:alternatives-cons 2)
    (operator bnf-v1:alternatives-nil 0)
    (operator bnf-v1:alternative 2)
    (operator bnf-v1:elements-nil 0)
    (operator bnf-v1:elements-cons 2)
    (operator bnf-v1:reference 2)
    (operator bnf-v1:literal 2))
  (equations)
  (rewrites
    (rule ebnf-reach-select (head (metta-equation
      (ebnf-reach-v1:select ?document ?start)
      (ebnf-reach-v1:prepare (ebnf-v1:declaration-view ?document) ?start ?document))) (body))
    (rule ebnf-reach-prepare (head (metta-equation
      (ebnf-reach-v1:prepare (bnf-v1:document ?entries ?span) ?start ?original)
      (ebnf-reach-v1:finish
        (ebnf-reach-v1:visit (ebnf-reach-v1:cons ?start (ebnf-reach-v1:nil)) (ebnf-reach-v1:nil) ?entries
          (ebnf-reach-v1:cons ?start (ebnf-reach-v1:names ?entries (ebnf-reach-v1:nil))))
        ?original))) (body))
    (rule ebnf-reach-finish (head (metta-equation
      (ebnf-reach-v1:finish ?names (bnf-v1:document ?entries ?span))
      (bnf-v1:document (ebnf-reach-v1:filter ?entries ?names) ?span))) (body))
    (rule ebnf-reach-visit-empty (head (metta-equation
      (ebnf-reach-v1:visit (ebnf-reach-v1:nil) ?seen ?entries ?fuel) ?seen)) (body))
    (rule ebnf-reach-visit-cons (head (metta-equation
      (ebnf-reach-v1:visit (ebnf-reach-v1:cons ?name ?todo) ?seen ?entries ?fuel)
      (ebnf-reach-v1:visit-next (ebnf-reach-v1:member ?name ?seen)
        (ebnf-reach-v1:cons ?name ?todo) ?seen ?entries ?fuel))) (body))
    (rule ebnf-reach-visit-known (head (metta-equation
      (ebnf-reach-v1:visit-next True (ebnf-reach-v1:cons ?name ?todo) ?seen ?entries ?fuel)
      (ebnf-reach-v1:visit ?todo ?seen ?entries ?fuel))) (body))
    (rule ebnf-reach-visit-new (head (metta-equation
      (ebnf-reach-v1:visit-next False (ebnf-reach-v1:cons ?name ?todo) ?seen ?entries
        (ebnf-reach-v1:cons ?spent ?fuel))
      (ebnf-reach-v1:visit (ebnf-reach-v1:dependencies ?entries ?name ?todo)
        (ebnf-reach-v1:cons ?name ?seen) ?entries ?fuel))) (body))
    (rule ebnf-reach-visit-exhausted (head (metta-equation
      (ebnf-reach-v1:visit-next False (ebnf-reach-v1:cons ?name ?todo) ?seen ?entries
        (ebnf-reach-v1:nil))
      (ebnf-reach-v1:names-exhausted ?name))) (body))
    ; Every name the entries reference, before the names given.
    (rule ebnf-reach-names-empty (head (metta-equation
      (ebnf-reach-v1:names (bnf-v1:entries-nil) ?names) ?names)) (body))
    (rule ebnf-reach-names-rule (head (metta-equation
      (ebnf-reach-v1:names (bnf-v1:entries-cons (bnf-v1:rule ?head (bnf-v1:expression ?alternatives ?espan) ?span) ?tail) ?names)
      (ebnf-reach-v1:alternative-names ?alternatives (ebnf-reach-v1:names ?tail ?names)))) (body))
    (rule ebnf-reach-names-comment (head (metta-equation
      (ebnf-reach-v1:names (bnf-v1:entries-cons (bnf-v1:comment ?text ?span) ?tail) ?names)
      (ebnf-reach-v1:names ?tail ?names))) (body))
    (rule ebnf-reach-names-blank (head (metta-equation
      (ebnf-reach-v1:names (bnf-v1:entries-cons (bnf-v1:blank ?span) ?tail) ?names)
      (ebnf-reach-v1:names ?tail ?names))) (body))
    (rule ebnf-reach-alternative-names-empty (head (metta-equation
      (ebnf-reach-v1:alternative-names (bnf-v1:alternatives-nil) ?names) ?names)) (body))
    (rule ebnf-reach-alternative-names-cons (head (metta-equation
      (ebnf-reach-v1:alternative-names (bnf-v1:alternatives-cons (bnf-v1:alternative ?elements ?aspan) ?tail) ?names)
      (ebnf-reach-v1:element-names ?elements (ebnf-reach-v1:alternative-names ?tail ?names)))) (body))
    (rule ebnf-reach-element-names-empty (head (metta-equation
      (ebnf-reach-v1:element-names (bnf-v1:elements-nil) ?names) ?names)) (body))
    (rule ebnf-reach-element-names-reference (head (metta-equation
      (ebnf-reach-v1:element-names (bnf-v1:elements-cons (bnf-v1:reference ?name ?span) ?tail) ?names)
      (ebnf-reach-v1:cons ?name (ebnf-reach-v1:element-names ?tail ?names)))) (body))
    (rule ebnf-reach-element-names-literal (head (metta-equation
      (ebnf-reach-v1:element-names (bnf-v1:elements-cons (bnf-v1:literal ?text ?span) ?tail) ?names)
      (ebnf-reach-v1:element-names ?tail ?names))) (body))
    (rule ebnf-reach-member-empty (head (metta-equation
      (ebnf-reach-v1:member ?name (ebnf-reach-v1:nil)) False)) (body))
    (rule ebnf-reach-member-cons (head (metta-equation
      (ebnf-reach-v1:member ?name (ebnf-reach-v1:cons ?head ?tail))
      (ebnf-reach-v1:member-next (langdef:ground-equal ?name ?head) ?name ?tail))) (body))
    (rule ebnf-reach-member-found (head (metta-equation
      (ebnf-reach-v1:member-next LangDef:SameValue ?name ?tail) True)) (body))
    (rule ebnf-reach-member-more (head (metta-equation
      (ebnf-reach-v1:member-next LangDef:DifferentValue ?name ?tail)
      (ebnf-reach-v1:member ?name ?tail))) (body))
    (rule ebnf-reach-dependencies-empty (head (metta-equation
      (ebnf-reach-v1:dependencies (bnf-v1:entries-nil) ?name ?todo) ?todo)) (body))
    (rule ebnf-reach-dependencies-rule (head (metta-equation
      (ebnf-reach-v1:dependencies (bnf-v1:entries-cons (bnf-v1:rule ?head ?expr ?span) ?tail) ?name ?todo)
      (ebnf-reach-v1:dependencies-next (langdef:ground-equal ?name ?head) ?expr ?tail ?name ?todo ?span))) (body))
    (rule ebnf-reach-dependencies-comment (head (metta-equation
      (ebnf-reach-v1:dependencies (bnf-v1:entries-cons (bnf-v1:comment ?text ?span) ?tail) ?name ?todo)
      (ebnf-reach-v1:dependencies ?tail ?name ?todo))) (body))
    (rule ebnf-reach-dependencies-blank (head (metta-equation
      (ebnf-reach-v1:dependencies (bnf-v1:entries-cons (bnf-v1:blank ?span) ?tail) ?name ?todo)
      (ebnf-reach-v1:dependencies ?tail ?name ?todo))) (body))
    (rule ebnf-reach-dependencies-found (head (metta-equation
      (ebnf-reach-v1:dependencies-next LangDef:SameValue
        (bnf-v1:expression (bnf-v1:alternatives-cons (bnf-v1:alternative ?elements ?aspan) (bnf-v1:alternatives-nil)) ?espan)
        ?tail ?name ?todo ?span)
      (ebnf-reach-v1:references ?elements ?todo))) (body))
    (rule ebnf-reach-dependencies-more (head (metta-equation
      (ebnf-reach-v1:dependencies-next LangDef:DifferentValue ?expr ?tail ?name ?todo ?span)
      (ebnf-reach-v1:dependencies ?tail ?name ?todo))) (body))
    (rule ebnf-reach-references-empty (head (metta-equation
      (ebnf-reach-v1:references (bnf-v1:elements-nil) ?todo) ?todo)) (body))
    (rule ebnf-reach-references-reference (head (metta-equation
      (ebnf-reach-v1:references (bnf-v1:elements-cons (bnf-v1:reference ?name ?span) ?tail) ?todo)
      (ebnf-reach-v1:cons ?name (ebnf-reach-v1:references ?tail ?todo)))) (body))
    (rule ebnf-reach-references-literal (head (metta-equation
      (ebnf-reach-v1:references (bnf-v1:elements-cons (bnf-v1:literal ?text ?span) ?tail) ?todo)
      (ebnf-reach-v1:references ?tail ?todo))) (body))
    (rule ebnf-reach-filter-empty (head (metta-equation
      (ebnf-reach-v1:filter (bnf-v1:entries-nil) ?names) (bnf-v1:entries-nil))) (body))
    (rule ebnf-reach-filter-rule (head (metta-equation
      (ebnf-reach-v1:filter (bnf-v1:entries-cons (bnf-v1:rule ?name ?expr ?span) ?tail) ?names)
      (ebnf-reach-v1:filter-next (ebnf-reach-v1:member ?name ?names)
        (bnf-v1:rule ?name ?expr ?span) ?tail ?names))) (body))
    (rule ebnf-reach-filter-comment (head (metta-equation
      (ebnf-reach-v1:filter (bnf-v1:entries-cons (bnf-v1:comment ?text ?span) ?tail) ?names)
      (bnf-v1:entries-cons (bnf-v1:comment ?text ?span) (ebnf-reach-v1:filter ?tail ?names)))) (body))
    (rule ebnf-reach-filter-blank (head (metta-equation
      (ebnf-reach-v1:filter (bnf-v1:entries-cons (bnf-v1:blank ?span) ?tail) ?names)
      (bnf-v1:entries-cons (bnf-v1:blank ?span) (ebnf-reach-v1:filter ?tail ?names)))) (body))
    (rule ebnf-reach-filter-keep (head (metta-equation
      (ebnf-reach-v1:filter-next True ?rule ?tail ?names)
      (bnf-v1:entries-cons ?rule (ebnf-reach-v1:filter ?tail ?names)))) (body))
    (rule ebnf-reach-filter-drop (head (metta-equation
      (ebnf-reach-v1:filter-next False ?rule ?tail ?names)
      (ebnf-reach-v1:filter ?tail ?names))) (body))))
