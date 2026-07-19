; Grammar-derived bridge from lib_parse's canonical binder IR to the shared
; ABT waist.  Parsing and lp-lower remain producers.  The final admission step
; independently checks the derived binding signature and closed scope in C.

!(import! &self lib_parse)

; Convert a structural Cons/Nil list to an expression payload for cons-atom.
(= (lp-abt:list-expr Nil) ())
(= (lp-abt:list-expr (Cons $x $xs))
   (let $tail (lp-abt:list-expr $xs)
     (cons-atom $x $tail)))

; HlB scopes the next ordinary hole with the named nonterminal.  Several
; pending binders with the same scope produce a field depth greater than one.
(= (lp-abt:pending-count $nt Nil) 0)
(= (lp-abt:pending-count $nt (Cons $scope $rest))
   (+ (if (== $nt $scope) 1 0)
      (lp-abt:pending-count $nt $rest)))
(= (lp-abt:pending-drop $nt Nil) Nil)
(= (lp-abt:pending-drop $nt (Cons $scope $rest))
   (if (== $nt $scope)
       (lp-abt:pending-drop $nt $rest)
       (Cons $scope (lp-abt:pending-drop $nt $rest))))

(= (lp-abt:template-depths $tpl)
   (lp-abt:template-depths-go $tpl Nil Nil))
(= (lp-abt:template-depths-go Nil $pending $depths-rev)
   (if (== $pending Nil)
       (Ok (rev $depths-rev))
       (Rejected dangling-binder-scope)))
(= (lp-abt:template-depths-go (Cons $token $rest) $pending $depths-rev)
   (case $token
     (((Tm $constant)
       (lp-abt:template-depths-go $rest $pending $depths-rev))
      ((HlB $nt $scope)
       (lp-abt:template-depths-go
         $rest (Cons $scope $pending) $depths-rev))
      ((Hl $nt)
       (lp-abt:template-depths-go
         $rest
         (lp-abt:pending-drop $nt $pending)
         (Cons (lp-abt:pending-count $nt $pending) $depths-rev)))
      ($other (Rejected bad-template)))))

; Generated fields use structural source positions because grammar holes need
; not have unique semantic names.  This stays inside the same BindingDecl
; language used by hand-authored packages.
(= (lp-abt:hole-fields Nil $index) Nil)
(= (lp-abt:hole-fields (Cons $depth $rest) $index)
   (Cons (Hole $index)
     (lp-abt:hole-fields $rest (+ $index 1))))
(= (lp-abt:binding-fields Nil $index) BNil)
(= (lp-abt:binding-fields (Cons $depth $rest) $index)
   (BCons (BField (Hole $index) $depth)
     (lp-abt:binding-fields $rest (+ $index 1))))
(= (lp-abt:signature-decl $label $depths)
   (let $field-list (lp-abt:hole-fields $depths 0)
     (let $field-expr (lp-abt:list-expr $field-list)
       (let $fields (cons-atom Fields $field-expr)
         (let $bindings (lp-abt:binding-fields $depths 0)
           (AbtSignature $label $fields (BindFields $bindings)))))))
(= (lp-abt:grammar-decls Nil) (Ok Nil))
(= (lp-abt:grammar-decls (Cons $entry $rest))
   (case $entry
     (((VarD $name $nt) (lp-abt:grammar-decls $rest))
      ((LexD $class $nt) (lp-abt:grammar-decls $rest))
      ((Prod $label $nt $tpl)
       (case (lp-abt:template-depths $tpl)
         (((Ok $depths)
           (case (lp-abt:grammar-decls $rest)
             (((Ok $decls)
               (Ok (Cons (lp-abt:signature-decl $label $depths) $decls)))
              ((Rejected $why) (Rejected $why))
              ($other (Rejected bad-grammar)))))
          ((Rejected $why) (Rejected $why))
          ($other (Rejected bad-template)))))
      ($other (Rejected bad-grammar-entry)))))

(= (lp-abt:signature-set $g)
   (case (lp-admit $g)
     ((Admitted
       (case (lp-abt:grammar-decls $g)
         (((Ok $decls)
           (let $decl-expr (lp-abt:list-expr $decls)
             (Ok (cons-atom AbtSignatures $decl-expr))))
          ((Rejected $why) (Rejected $why))
          ($other (Rejected bad-grammar)))))
      ((Rejected $why) (Rejected $why))
      ($other (Rejected bad-grammar)))))

; Convert a canonical lp-lower tree.  Binder markers are grammar metadata and
; therefore disappear; Bv becomes Var, while free leaves become explicit Con.
(= (lp-abt:tree-kids $g Nil Nil) (Ok Nil))
(= (lp-abt:tree-kids $g Nil (Cons $kid $kids))
   (Rejected extra-canonical-child))
(= (lp-abt:tree-kids-binder $g $nt $scope $rest $kids)
   (case $kids
     ((Nil (Rejected missing-binder-marker))
      ((Cons $kid $kid-rest)
       (case $kid
         (((Bd $got-nt $got-scope)
           (if (and (== $got-nt $nt) (== $got-scope $scope))
               (lp-abt:tree-kids $g $rest $kid-rest)
               (Rejected binder-marker-mismatch)))
          ($other (Rejected binder-marker-mismatch)))))
      ($other (Rejected bad-canonical-children)))))
(= (lp-abt:tree-kids-hole $g $rest $kids)
   (case $kids
     ((Nil (Rejected missing-canonical-child))
      ((Cons $kid $kid-rest)
       (case (lp-abt:tree-go $g $kid)
         (((Ok $term)
           (case (lp-abt:tree-kids $g $rest $kid-rest)
             (((Ok $terms) (Ok (Cons $term $terms)))
              ((Rejected $why) (Rejected $why))
              ($other (Rejected bad-canonical-tree)))))
          ((Rejected $why) (Rejected $why))
          ($other (Rejected bad-canonical-tree)))))
      ($other (Rejected bad-canonical-children)))))
(= (lp-abt:tree-kids $g (Cons $token $rest) $kids)
   (case $token
     (((Tm $constant) (lp-abt:tree-kids $g $rest $kids))
      ((HlB $nt $scope)
       (lp-abt:tree-kids-binder $g $nt $scope $rest $kids))
      ((Hl $nt) (lp-abt:tree-kids-hole $g $rest $kids))
      ($other (Rejected bad-template)))))

(= (lp-abt:tree-node $g $label $kids)
   (case (template-of $g $label)
     (((Err $why) (Rejected unknown-production))
      ($tpl
       (case (lp-abt:tree-kids $g $tpl $kids)
         (((Ok $terms)
           (let $term-expr (lp-abt:list-expr $terms)
             (Ok (cons-atom $label $term-expr))))
          ((Rejected $why) (Rejected $why))
          ($other (Rejected bad-canonical-tree))))))))
(= (lp-abt:tree-go $g $tree)
   (case $tree
     (((Lf $value) (Ok (Con $value)))
      ((Bv $index) (Ok (Var $index)))
      ((Nd $label $kids) (lp-abt:tree-node $g $label $kids))
      ($other (Rejected bad-canonical-tree)))))

(= (lp-abt:bridge $g $canonical-tree)
   (case (lp-abt:signature-set $g)
     (((Ok $signature)
       (case (lp-abt:tree-go $g $canonical-tree)
         (((Ok $term) (Ok (AbtBridge $signature $term)))
          ((Rejected $why) (Rejected $why))
          ($other (Rejected bad-canonical-tree)))))
      ((Rejected $why) (Rejected $why))
      ($other (Rejected bad-grammar)))))

; CeTTa admission boundary: both the derived table and the resulting closed
; term are rechecked by the neutral C ABT mechanism.
(= (lp-abt:admit-canonical $g $canonical-tree)
   (case (lp-abt:bridge $g $canonical-tree)
     (((Ok (AbtBridge $signature $term))
       (if (== (__cetta_abt_signature_admitted $signature) True)
           (if (== (__cetta_abt_scope_check $signature 0 $term) True)
               (Ok $term)
               (Rejected invalid-abt-scope))
           (Rejected invalid-abt-signature)))
      ((Rejected $why) (Rejected $why))
      ($other (Rejected bad-abt-bridge)))))

(= (lp-abt:admit-raw $g $raw-tree)
   (case (lp-admit $g)
     ((Admitted
       (if (tree-ok $g $raw-tree)
           (lp-abt:admit-canonical $g (lp-lower $g $raw-tree))
           (Rejected bad-raw-tree)))
      ((Rejected $why) (Rejected $why))
      ($other (Rejected bad-grammar)))))

; Compose a replayed shared parser certificate with the ABT admission boundary.
; Recognition remains an untrusted producer: the certificate is reconstructed
; against grammar data before binder lowering, and the resulting term is then
; scope-checked by the neutral native ABT mechanism.  Failures retain both the
; phase and the rejected object so callers can inspect them as data.
(= (lp-abt:admit-shared-cert $g $nt $toks $cert)
   (case (lp-shared-cert-tree $g $nt $toks $cert)
     (((Ok $raw-tree)
       (case (lp-abt:admit-raw $g $raw-tree)
         (((Ok $term) (Ok $term))
          ((Rejected $why)
           (Rejected (LPABTError abt-admission $raw-tree $why)))
          ($other
           (Rejected
             (LPABTError abt-admission $raw-tree bad-result))))))
      ((Err $why)
       (Rejected (LPABTError certificate-replay $cert $why)))
      ($other
       (Rejected (LPABTError certificate-replay $cert bad-result))))))

; Preserve the generalized parser's result class.  Only a unique, replayable
; certificate crosses into ABT admission; ambiguity and absence remain distinct
; parser outcomes rather than being collapsed into rejection.
(= (lp-abt:admit-shared-result $g $nt $toks $result)
   (case $result
     (((Unique $cert)
       (case (lp-abt:admit-shared-cert $g $nt $toks $cert)
         (((Ok $term) (Unique $term))
          ((Rejected $why) (Rejected $why))
          ($other
           (Rejected (LPABTError shared-result $other bad-result))))))
      (Ambiguous Ambiguous)
      (NoParse NoParse)
      ($other
       (Rejected (LPABTError shared-result $other unsupported-result))))))
