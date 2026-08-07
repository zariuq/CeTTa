; Generic proof-rule machine.  Guest theories provide source-block/2 facts.
; Source artifacts and bytecode artifacts are immutable link chains, so an old
; artifact remains a revision-pinned snapshot after a new block is published.

(gslt-presentation-v1 RuleMachineCoreV1
  (signature
    (operator artifact-empty 1)
    (operator artifact-link 2)
    (operator bc-artifact-empty 1)
    (operator bc-artifact-link 2)
    (operator bc-block-ref 2)
    (operator bc-block 6)
    (operator bc-build 1)
    (operator bc-goals 1)
    (operator bc-match 1)
    (operator bc-observe 5)
    (operator bc-solve 4)
    (operator bc-solve-premises 3)
    (operator bc-visible-block 2)
    (operator compile-artifact 2)
    (operator compile-block 2)
    (operator compile-link 3)
    (operator compile-source-block 3)
    (operator guest-bc-observe 6)
    (operator guest-bytecode 4)
    (operator guest-observe 6)
    (operator guest-revision 3)
    (operator nat-s 1)
    (operator observe 5)
    (operator occurrence 1)
    (operator rm-block 5)
    (operator block-ref 2)
    (operator rm-cons 2)
    (operator rm-premise 2)
    (operator rm-proof-app 2)
    (operator rm-proof-atom 1)
    (operator solve 4)
    (operator solve-premises 3)
    (operator source-block 2)
    (operator source-block-id 3)
    (operator visible-block 2))

  (equations)

  (rewrites
    ; A link exposes its new block and every block visible in its parent.
    (rule visible-block-here
      (head
        (visible-block
          (artifact-link ?parent (block-ref ?guest ?id)) ?block))
      (body (source-block-id ?guest ?id ?block)))
    (rule visible-block-parent
      (head (visible-block (artifact-link ?parent ?block) ?visible))
      (body (visible-block ?parent ?visible)))

    ; A zero-premise block is an axiom.  A rule consumes one unit of depth;
    ; every premise is checked at the same remaining depth.
    (rule solve-axiom
      (head (solve ?artifact ?depth ?goal ?proof))
      (body
        (visible-block
          ?artifact
          (rm-block ?id ?source ?proof rm-nil ?goal))))
    (rule solve-rule
      (head (solve ?artifact (nat-s ?depth) ?goal ?proof))
      (body
        (visible-block
          ?artifact
          (rm-block ?id ?source ?proof (rm-cons ?first ?rest) ?goal))
        (solve-premises
          ?artifact ?depth (rm-cons ?first ?rest))))
    (rule solve-premises-empty
      (head (solve-premises ?artifact ?depth rm-nil))
      (body))
    (rule solve-premises-step
      (head
        (solve-premises
          ?artifact
          ?depth
          (rm-cons (rm-premise ?proof ?goal) ?rest)))
      (body
        (solve ?artifact ?depth ?goal ?proof)
        (solve-premises ?artifact ?depth ?rest)))

    ; Occurrences retain their proof term.  No set quotient is introduced.
    (rule observe-proof-occurrence
      (head
        (observe
          proof-occurrence-bag ?artifact ?depth ?goal (occurrence ?proof)))
      (body (solve ?artifact ?depth ?goal ?proof)))

    ; Compilation changes representation: a bytecode block contains an
    ; explicit match, goal-list, proof-build, and emit instruction.
    (rule compile-block-generic
      (head
        (compile-block
          (rm-block ?id ?source ?proof ?premises ?conclusion)
          (bc-block
            ?id
            ?source
            (bc-match ?conclusion)
            (bc-goals ?premises)
            (bc-build ?proof)
            bc-emit)))
      (body))
    (rule compile-source-block-generic
      (head (compile-source-block ?guest ?source ?bytecode))
      (body
        (source-block ?guest ?source)
        (compile-block ?source ?bytecode)))

    (rule source-block-by-id
      (head
        (source-block-id
          ?guest ?id (rm-block ?id ?source ?proof ?premises ?conclusion)))
      (body
        (source-block
          ?guest (rm-block ?id ?source ?proof ?premises ?conclusion))))

    ; Full compilation is useful for cold builds.  compile-link is the
    ; incremental publication relation: it compiles exactly the supplied
    ; source block and reuses the already-compiled parent artifact unchanged.
    (rule compile-artifact-empty
      (head
        (compile-artifact
          (artifact-empty ?guest)
          (bc-artifact-empty ?guest)))
      (body))
    (rule compile-artifact-link
      (head
        (compile-artifact
          (artifact-link ?parent (block-ref ?guest ?id))
          (bc-artifact-link
            ?compiled-parent (bc-block-ref ?guest ?id))))
      (body
        (compile-artifact ?parent ?compiled-parent)
        (source-block-id ?guest ?id ?source)
        (compile-block ?source ?compiled-block)))
    (rule compile-link-one-block
      (head
        (compile-link
          ?compiled-parent
          (block-ref ?guest ?id)
          (bc-artifact-link
            ?compiled-parent (bc-block-ref ?guest ?id))))
      (body
        (source-block-id ?guest ?id ?source)
        (compile-block ?source ?compiled-block)))

    (rule bc-visible-block-here
      (head
        (bc-visible-block
          (bc-artifact-link ?parent (bc-block-ref ?guest ?id)) ?block))
      (body
        (source-block-id ?guest ?id ?source)
        (compile-block ?source ?block)))
    (rule bc-visible-block-parent
      (head
        (bc-visible-block
          (bc-artifact-link ?parent ?block) ?visible))
      (body (bc-visible-block ?parent ?visible)))

    ; Bytecode execution is specified independently over instruction-shaped
    ; fields.  It does not consult source rm-block values.
    (rule bc-solve-axiom
      (head (bc-solve ?artifact ?depth ?goal ?proof))
      (body
        (bc-visible-block
          ?artifact
          (bc-block
            ?id ?source (bc-match ?goal) (bc-goals rm-nil)
            (bc-build ?proof) bc-emit))))
    (rule bc-solve-rule
      (head (bc-solve ?artifact (nat-s ?depth) ?goal ?proof))
      (body
        (bc-visible-block
          ?artifact
          (bc-block
            ?id ?source (bc-match ?goal)
            (bc-goals (rm-cons ?first ?rest))
            (bc-build ?proof) bc-emit))
        (bc-solve-premises
          ?artifact ?depth (rm-cons ?first ?rest))))
    (rule bc-solve-premises-empty
      (head (bc-solve-premises ?artifact ?depth rm-nil))
      (body))
    (rule bc-solve-premises-step
      (head
        (bc-solve-premises
          ?artifact
          ?depth
          (rm-cons (rm-premise ?proof ?goal) ?rest)))
      (body
        (bc-solve ?artifact ?depth ?goal ?proof)
        (bc-solve-premises ?artifact ?depth ?rest)))
    (rule bc-observe-proof-occurrence
      (head
        (bc-observe
          proof-occurrence-bag ?artifact ?depth ?goal (occurrence ?proof)))
      (body (bc-solve ?artifact ?depth ?goal ?proof)))

    (rule guest-observe-revision
      (head
        (guest-observe
          proof-occurrence-bag ?guest ?revision ?depth ?goal ?occurrence))
      (body
        (guest-revision ?guest ?revision ?artifact)
        (observe
          proof-occurrence-bag ?artifact ?depth ?goal ?occurrence)))
    (rule guest-compile-revision
      (head
        (guest-bytecode ?guest ?revision ?artifact ?bytecode))
      (body
        (guest-revision ?guest ?revision ?artifact)
        (compile-artifact ?artifact ?bytecode)))
    (rule guest-bc-observe-revision
      (head
        (guest-bc-observe
          proof-occurrence-bag ?guest ?revision ?depth ?goal ?occurrence))
      (body
        (guest-bytecode ?guest ?revision ?artifact ?bytecode)
        (bc-observe
          proof-occurrence-bag ?bytecode ?depth ?goal ?occurrence)))
  ))
