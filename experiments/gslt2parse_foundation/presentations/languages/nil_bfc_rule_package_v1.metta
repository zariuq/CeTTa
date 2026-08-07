; Nil Geisweiller's fixed Hilbert package, expressed as generic proof blocks.
; The guest presentation contains no machine instruction or search opcode.

(gslt-presentation-v1 NilBFCRulePackageV1
  (signature
    (operator imp 2)
    (operator neg 1))
  (equations)
  (rewrites
    (rule nil-bfc-source-ax1
      (head
        (source-block
          nil-bfc
          (rm-block ax1 bfc-xp.mm2:ax1
            (rm-proof-atom ax1)
            rm-nil
            (imp ?p (imp ?q ?p)))))
      (body))
    (rule nil-bfc-source-ax2
      (head
        (source-block
          nil-bfc
          (rm-block ax2 bfc-xp.mm2:ax2
            (rm-proof-atom ax2)
            rm-nil
            (imp
              (imp ?p (imp ?q ?r))
              (imp (imp ?p ?q) (imp ?p ?r))))))
      (body))
    (rule nil-bfc-source-ax3
      (head
        (source-block
          nil-bfc
          (rm-block ax3 bfc-xp.mm2:ax3
            (rm-proof-atom ax3)
            rm-nil
            (imp (imp (neg ?p) (neg ?q)) (imp ?q ?p)))))
      (body))
    (rule nil-bfc-source-mp
      (head
        (source-block
          nil-bfc
          (rm-block mpi bfc-xp.mm2:mpi
            (rm-proof-app mpi (rm-cons ?left (rm-cons ?right rm-nil)))
            (rm-cons
              (rm-premise ?left ?antecedent)
              (rm-cons
                (rm-premise ?right (imp ?antecedent ?conclusion))
                rm-nil))
            ?conclusion)))
      (body))
    (rule nil-bfc-r0
      (head
        (guest-revision nil-bfc r0
          (artifact-link
            (artifact-link
              (artifact-link
                (artifact-link
                  (artifact-empty nil-bfc)
                  (block-ref nil-bfc ax1))
                (block-ref nil-bfc ax2))
              (block-ref nil-bfc ax3))
            (block-ref nil-bfc mpi))))
      (body))
  ))
