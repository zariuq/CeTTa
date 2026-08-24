; generated finite-Horn facts from a canonical answer stream
(gslt-presentation-v1 GeneratedAnswerFactsV1_43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76
  (signature
    (operator state-action-v1 3)
    (operator state-byte-literal-v1 1)
    (operator state-check-proof-compressed-v1 5)
    (operator state-check-proof-normal-v1 4)
    (operator state-copy-each-row-matching-v1 6)
    (operator state-copy-each-row-v1 4)
    (operator state-insert-each-matching-v1 6)
    (operator state-insert-each-v1 4)
    (operator state-insert-row-v1 3)
    (operator state-insert-unordered-pairs-v1 3)
    (operator state-literal-v1 1)
    (operator state-proof-decoder-v1 13)
    (operator state-proof-kinds-v1 6)
    (operator state-proof-machine-v1 2)
    (operator state-proof-tables-v1 9)
    (operator state-require-depth-v1 1)
    (operator state-require-each-key-absent-v1 3)
    (operator state-require-each-v1 3)
    (operator state-require-row-key-absent-v1 2)
    (operator state-require-row-v1 2)
    (operator state-resolve-source-v1 3)
    (operator state-role-at-v1 2)
    (operator state-role-list-v1 2)
    (operator state-row-cons-v1 2)
    (operator state-source-column-v1 1)
    (operator state-stack-proof-machine-v1 3)
    (operator state-table-v1 4)
  )
  (equations)
  (rewrites
    (rule answer-fact-b8754bff4d4e22c6241bd27b4854915f6ffc72a8f607de7a26ff7f65d95deb95
      (head (state-action-v1 mm-check-theorem-compressed 0 (state-require-row-key-absent-v1 mm-state-symbol-kind (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-7f05f9c06636fc4c9f3afa791ad7ea2d811a2ed8c559f0ada7d3584c3da084d2
      (head (state-action-v1 mm-check-theorem-compressed 1 (state-require-row-key-absent-v1 mm-state-label-name (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-6a486486610709e0fde41fc8273d3ae39457e03c8697e58bd2bd49fe89be7c66
      (head (state-action-v1 mm-check-theorem-compressed 10 (state-copy-each-row-matching-v1 mm-state-active-hypothesis mm-state-assertion-frame-hypothesis (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)) mm-state-assertion-ordered-frame-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 state-source-match-index-v1 (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1))))))
      (body))
    (rule answer-fact-920ee4bdeb3173d5cdd89be02a60adbfd336043ba6966b589637bb6058c17fc7
      (head (state-action-v1 mm-check-theorem-compressed 11 (state-copy-each-row-matching-v1 mm-state-disjoint-variable mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)) mm-state-assertion-disjoint-left-candidate state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 1) state-row-nil-v1))))))
      (body))
    (rule answer-fact-8bfc225eb87aea9c582ba13a0e5ef504316442f5fc445a5ee0f16e1492c5996e
      (head (state-action-v1 mm-check-theorem-compressed 12 (state-copy-each-row-matching-v1 mm-state-assertion-disjoint-left-candidate mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)) mm-state-assertion-disjoint-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 1) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1))))))
      (body))
    (rule answer-fact-0c9711791f1a7a0e4fe69ce878f89cb5ac294a789633aabe71bf690c3b3c5481
      (head (state-action-v1 mm-check-theorem-compressed 13 (state-copy-each-row-matching-v1 mm-state-assertion-disjoint-left-candidate mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)) mm-state-assertion-ordered-disjoint-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 state-source-match-index-v1 (state-row-cons-v1 (state-source-column-v1 1) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)))))))
      (body))
    (rule answer-fact-c384d77707765881dc48e222ae1d646fc24ce1cbb724069edbeedd598a173b2c
      (head (state-action-v1 mm-check-theorem-compressed 14 (state-check-proof-compressed-v1 mm-stack-proof-machine-v1 mm-label mm-symbol mm-proof-step mm-compressed-word)))
      (body))
    (rule answer-fact-a5b767dc59cdbcb508601363695c7177e1f1dbc7cd2bbaaf42eb02dd03eeaa64
      (head (state-action-v1 mm-check-theorem-compressed 15 (state-insert-row-v1 mm-state-formula state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-role-list-v1 mm-symbol 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-05a965ac1c07ab4185ce4bdc124c38c7f742885ec0741a75ed294c61d300d1ef
      (head (state-action-v1 mm-check-theorem-compressed 16 (state-insert-row-v1 mm-state-label-name state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-1c5904e9e495841391767c52a9097c0fda0c1e9ec7e57f68cc4763a7767a9072
      (head (state-action-v1 mm-check-theorem-compressed 17 (state-insert-row-v1 mm-state-label-kind state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-literal-v1 mm-label-theorem) state-row-nil-v1)))))
      (body))
    (rule answer-fact-8a1896049ba8cb7a63d1442121e90b83b42b5e8805c098aa3ffe90e71bcf89ad
      (head (state-action-v1 mm-check-theorem-compressed 2 (state-require-row-v1 mm-state-symbol-kind (state-row-cons-v1 (state-role-at-v1 mm-symbol 0) (state-row-cons-v1 (state-literal-v1 mm-symbol-constant) state-row-nil-v1)))))
      (body))
    (rule answer-fact-2ac79198ac5770fbce2e06c6c9f65f660b1370cda16690a2bd21dd8757ae7bb9
      (head (state-action-v1 mm-check-theorem-compressed 3 (state-require-each-v1 mm-symbol mm-state-expression-symbol (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule answer-fact-becad5c69d09b4d4f30e0b722e1f47e87999779ccdd0d95913dd43e2df1195d7
      (head (state-action-v1 mm-check-theorem-compressed 4 (state-insert-each-matching-v1 mm-symbol mm-state-symbol-kind (state-row-cons-v1 state-input-v1 (state-row-cons-v1 (state-literal-v1 mm-symbol-variable) state-row-nil-v1)) mm-state-assertion-mandatory-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 state-input-v1 state-row-nil-v1)))))
      (body))
    (rule answer-fact-70388eaf297e62bc6101d35b97b1a214b77eba0e69534fa82b7d1a7fc355efb9
      (head (state-action-v1 mm-check-theorem-compressed 5 (state-copy-each-row-v1 mm-state-active-essential-variable mm-state-assertion-mandatory-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-dc80f7950af4c3d2f138a0681daa921bec4571c6a0bc2dc5aeda198eb38d2fd4
      (head (state-action-v1 mm-check-theorem-compressed 6 (state-copy-each-row-v1 mm-state-active-hypothesis mm-state-assertion-active-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-a7771f7881aad2146db57210bd41c4f15f331e2a8817b03c9ce3663ce123758c
      (head (state-action-v1 mm-check-theorem-compressed 7 (state-copy-each-row-v1 mm-state-active-hypothesis mm-state-assertion-ordered-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 state-source-row-index-v1 (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1))))))
      (body))
    (rule answer-fact-ce06dbc68fec9e71ac491b313cec63f424e1c244a33e2ff50e31461ef4bbad30
      (head (state-action-v1 mm-check-theorem-compressed 8 (state-copy-each-row-matching-v1 mm-state-active-hypothesis mm-state-label-kind (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-literal-v1 mm-label-essential) state-row-nil-v1)) mm-state-assertion-frame-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-03c5c18a1b38b82aca71e326345032e7f3015010583e641254182b03fb4c1cbe
      (head (state-action-v1 mm-check-theorem-compressed 9 (state-copy-each-row-matching-v1 mm-state-active-hypothesis mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)) mm-state-assertion-frame-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-204c9c4a49c5326f8b665e62b1195ebc4a80c206a501e42ccce7e1d1c6dbc4ac
      (head (state-action-v1 mm-check-theorem-normal 0 (state-require-row-key-absent-v1 mm-state-symbol-kind (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-7326b92af5e093ad6a437ad0a8b1fef4ff6555fd6fd1a9f9f6b560fc920dcabd
      (head (state-action-v1 mm-check-theorem-normal 1 (state-require-row-key-absent-v1 mm-state-label-name (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-9b80d98a2a9d6feef5092e3941cde0667c1d73d7a9176dff4510499076993fbb
      (head (state-action-v1 mm-check-theorem-normal 10 (state-copy-each-row-matching-v1 mm-state-active-hypothesis mm-state-assertion-frame-hypothesis (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)) mm-state-assertion-ordered-frame-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 state-source-match-index-v1 (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1))))))
      (body))
    (rule answer-fact-d62a31aa45cedff3f92130269475178240eae5bdc00486540ed1a0e407d9323c
      (head (state-action-v1 mm-check-theorem-normal 11 (state-copy-each-row-matching-v1 mm-state-disjoint-variable mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)) mm-state-assertion-disjoint-left-candidate state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 1) state-row-nil-v1))))))
      (body))
    (rule answer-fact-317d21e60a9cc5f0f44c353b18fe9100aa74839819f68d4fd574cb24b0524c34
      (head (state-action-v1 mm-check-theorem-normal 12 (state-copy-each-row-matching-v1 mm-state-assertion-disjoint-left-candidate mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)) mm-state-assertion-disjoint-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 1) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1))))))
      (body))
    (rule answer-fact-4c0d7c5ab8c2bfce8705a1a3df0add6d737eb74ebcb3d2aef9d305f7fcfaaac8
      (head (state-action-v1 mm-check-theorem-normal 13 (state-copy-each-row-matching-v1 mm-state-assertion-disjoint-left-candidate mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)) mm-state-assertion-ordered-disjoint-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 state-source-match-index-v1 (state-row-cons-v1 (state-source-column-v1 1) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)))))))
      (body))
    (rule answer-fact-dbf114986f8d9855a2974107be1114dc6c134eceb9bbb76b9213ac61d6fc8a4c
      (head (state-action-v1 mm-check-theorem-normal 14 (state-check-proof-normal-v1 mm-stack-proof-machine-v1 mm-label mm-symbol mm-proof-step)))
      (body))
    (rule answer-fact-30df07bbdc445351140e7f23adc3793d65de560e74d4f77fa36d9429382da09a
      (head (state-action-v1 mm-check-theorem-normal 15 (state-insert-row-v1 mm-state-formula state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-role-list-v1 mm-symbol 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-ac0dbea3276061f094018f2d4fef171a18df1906db2b602d45382a688586f61c
      (head (state-action-v1 mm-check-theorem-normal 16 (state-insert-row-v1 mm-state-label-name state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-3d3ed683b8a8f788120c2659041bffe027407bb9752b50c4b2047c4b0167d3fa
      (head (state-action-v1 mm-check-theorem-normal 17 (state-insert-row-v1 mm-state-label-kind state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-literal-v1 mm-label-theorem) state-row-nil-v1)))))
      (body))
    (rule answer-fact-9684b7ef154b2480031561050e28af9859dbc7033b103f3d2c1371d8db2870d8
      (head (state-action-v1 mm-check-theorem-normal 2 (state-require-row-v1 mm-state-symbol-kind (state-row-cons-v1 (state-role-at-v1 mm-symbol 0) (state-row-cons-v1 (state-literal-v1 mm-symbol-constant) state-row-nil-v1)))))
      (body))
    (rule answer-fact-b41f7049ee49082e558b54ef4c4b5dce21f90749f1545b4eb9feb3fa7e9f3376
      (head (state-action-v1 mm-check-theorem-normal 3 (state-require-each-v1 mm-symbol mm-state-expression-symbol (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule answer-fact-6ebd670f206e63ea78ef92ef629a804a58356e026346af0fbd27ac2df5caf08f
      (head (state-action-v1 mm-check-theorem-normal 4 (state-insert-each-matching-v1 mm-symbol mm-state-symbol-kind (state-row-cons-v1 state-input-v1 (state-row-cons-v1 (state-literal-v1 mm-symbol-variable) state-row-nil-v1)) mm-state-assertion-mandatory-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 state-input-v1 state-row-nil-v1)))))
      (body))
    (rule answer-fact-dbcd2bf2d9ecdb6ff9bbac4574a6905116659d0679e4180a24cb6788b0f5892a
      (head (state-action-v1 mm-check-theorem-normal 5 (state-copy-each-row-v1 mm-state-active-essential-variable mm-state-assertion-mandatory-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-70a6a9fcf5becafadb606681e8e6513edc627a5ca7c82eb224fa7174b68a83aa
      (head (state-action-v1 mm-check-theorem-normal 6 (state-copy-each-row-v1 mm-state-active-hypothesis mm-state-assertion-active-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-67cf30c0ee7da0f91843024b9e78ea2f780db82b1711a791c6db4272d8330097
      (head (state-action-v1 mm-check-theorem-normal 7 (state-copy-each-row-v1 mm-state-active-hypothesis mm-state-assertion-ordered-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 state-source-row-index-v1 (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1))))))
      (body))
    (rule answer-fact-441fdf4a48b5f75d008e2e40e08850ece488432df93417a7f612c591c5c48483
      (head (state-action-v1 mm-check-theorem-normal 8 (state-copy-each-row-matching-v1 mm-state-active-hypothesis mm-state-label-kind (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-literal-v1 mm-label-essential) state-row-nil-v1)) mm-state-assertion-frame-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-33fb65ab3832c86129117626e59cbe5e028b72978bc1e9adfce7fa3be7a405b8
      (head (state-action-v1 mm-check-theorem-normal 9 (state-copy-each-row-matching-v1 mm-state-active-hypothesis mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)) mm-state-assertion-frame-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-3761c9e35365b3733636cb21eedf66adfd3e7a6fbdd54d8234ae2f6dea25df73
      (head (state-action-v1 mm-close-scope 0 state-pop-scope-v1))
      (body))
    (rule answer-fact-3507952e5c915c16772f025a4f443a7584d20d7662df2661f25ed37fa8ca316b
      (head (state-action-v1 mm-complete-block 0 state-noop-v1))
      (body))
    (rule answer-fact-624168920da4ff0a7c97ecc8d0195ea995f0dbbbdfbdbe0654f0fe15efa638ae
      (head (state-action-v1 mm-declare-axiom 0 (state-require-row-key-absent-v1 mm-state-symbol-kind (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-c42960bb17f929f21681e6a26d0b65598f4db9560f45b44bb93fb1049ee99241
      (head (state-action-v1 mm-declare-axiom 1 (state-require-row-key-absent-v1 mm-state-label-name (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-177d5797b0ddc3512bba856dbff49e45db640593ed66479712dc458255c0f816
      (head (state-action-v1 mm-declare-axiom 10 (state-copy-each-row-matching-v1 mm-state-disjoint-variable mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)) mm-state-assertion-disjoint-left-candidate state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 1) state-row-nil-v1))))))
      (body))
    (rule answer-fact-bd9535499465152e8313ed3c4d74a6f024a593325f5efe6debf9a2e794ac5d65
      (head (state-action-v1 mm-declare-axiom 11 (state-copy-each-row-matching-v1 mm-state-assertion-disjoint-left-candidate mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)) mm-state-assertion-disjoint-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 1) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1))))))
      (body))
    (rule answer-fact-8bec7bd671d80d40060b95f306dc54b4c5fff8b4cba100dd3deae46e670e1b9e
      (head (state-action-v1 mm-declare-axiom 12 (state-copy-each-row-matching-v1 mm-state-assertion-disjoint-left-candidate mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)) mm-state-assertion-ordered-disjoint-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 state-source-match-index-v1 (state-row-cons-v1 (state-source-column-v1 1) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)))))))
      (body))
    (rule answer-fact-05c73b3c2b6e734a1a0f0e1090371560ef5f361134f6a767850e5ef864f90ff8
      (head (state-action-v1 mm-declare-axiom 13 (state-insert-row-v1 mm-state-formula state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-role-list-v1 mm-symbol 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-85a1a26358fc0e05ade43218589fc5de692e3ce110836b854fe1ae1e5caaff8e
      (head (state-action-v1 mm-declare-axiom 14 (state-insert-row-v1 mm-state-label-name state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-be6cc7ca111389944ccd9f71b8a6171b52748ad5f8eb096ba2569872a4a082f2
      (head (state-action-v1 mm-declare-axiom 15 (state-insert-row-v1 mm-state-label-kind state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-literal-v1 mm-label-axiom) state-row-nil-v1)))))
      (body))
    (rule answer-fact-9e56f2ea8ac2e02d5553d8c40640b5c6194ffb25e75ffc7c6d8be392e26d9a45
      (head (state-action-v1 mm-declare-axiom 2 (state-require-row-v1 mm-state-symbol-kind (state-row-cons-v1 (state-role-at-v1 mm-symbol 0) (state-row-cons-v1 (state-literal-v1 mm-symbol-constant) state-row-nil-v1)))))
      (body))
    (rule answer-fact-93e7ca05dacf3154b63720408a540eef29a7b76963db2bc974b6df1a6030b2b2
      (head (state-action-v1 mm-declare-axiom 3 (state-require-each-v1 mm-symbol mm-state-expression-symbol (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule answer-fact-22418fc009a59e253b76a804613556439b33ee0b3f2991ada03ff1c98c49c25b
      (head (state-action-v1 mm-declare-axiom 4 (state-insert-each-matching-v1 mm-symbol mm-state-symbol-kind (state-row-cons-v1 state-input-v1 (state-row-cons-v1 (state-literal-v1 mm-symbol-variable) state-row-nil-v1)) mm-state-assertion-mandatory-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 state-input-v1 state-row-nil-v1)))))
      (body))
    (rule answer-fact-249882003f77042023be34768f6f8d9a970afd6e9586d129a27aa6214bd2ed22
      (head (state-action-v1 mm-declare-axiom 5 (state-copy-each-row-v1 mm-state-active-essential-variable mm-state-assertion-mandatory-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-92f5463e7e7605b6f168c779a082c22b786e268a9b5e64cc308f3fb590f0802c
      (head (state-action-v1 mm-declare-axiom 6 (state-copy-each-row-v1 mm-state-active-hypothesis mm-state-assertion-ordered-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 state-source-row-index-v1 (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1))))))
      (body))
    (rule answer-fact-d07589e6aabfb315433185ba94a8e3519c44a94d5e04504e94ee30ab6fbe8ef2
      (head (state-action-v1 mm-declare-axiom 7 (state-copy-each-row-matching-v1 mm-state-active-hypothesis mm-state-label-kind (state-row-cons-v1 (state-source-column-v1 0) (state-row-cons-v1 (state-literal-v1 mm-label-essential) state-row-nil-v1)) mm-state-assertion-frame-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-d2ceb3155ffad73ed35bf4fbac0afdbdcd82e9f308f18b740689a7c875866a2b
      (head (state-action-v1 mm-declare-axiom 8 (state-copy-each-row-matching-v1 mm-state-active-hypothesis mm-state-assertion-mandatory-variable (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 2) state-row-nil-v1)) mm-state-assertion-frame-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-d18e2378dae50060915e19146b09df94dae945813f37cac338fe9232d099e318
      (head (state-action-v1 mm-declare-axiom 9 (state-copy-each-row-matching-v1 mm-state-active-hypothesis mm-state-assertion-frame-hypothesis (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1)) mm-state-assertion-ordered-frame-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 state-source-match-index-v1 (state-row-cons-v1 (state-source-column-v1 0) state-row-nil-v1))))))
      (body))
    (rule answer-fact-b8d70e9de493a0c5b7c524a93edbb0b69f14d8d4b40617d721917d3df85dd74b
      (head (state-action-v1 mm-declare-constants 0 (state-require-depth-v1 0)))
      (body))
    (rule answer-fact-39080659bd29af2d56b2ab5cc4e4229812461c9969b4b88c26e09e84c18f1685
      (head (state-action-v1 mm-declare-constants 1 (state-require-each-key-absent-v1 mm-symbol mm-state-label-name (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule answer-fact-a0570df74acdda811bc6b97988a316295847a990f8266d5e5e2c0ed9287289f1
      (head (state-action-v1 mm-declare-constants 2 (state-insert-each-v1 mm-symbol mm-state-symbol-kind state-require-key-absent-v1 (state-row-cons-v1 state-input-v1 (state-row-cons-v1 (state-literal-v1 mm-symbol-constant) state-row-nil-v1)))))
      (body))
    (rule answer-fact-8262c2ed17b27ec045e390589c41962a20b1f4c53fe337fb9bfe1019802e15ac
      (head (state-action-v1 mm-declare-constants 3 (state-insert-each-v1 mm-symbol mm-state-expression-symbol state-insert-or-require-equal-v1 (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule answer-fact-46ddefaca09476472e31559be31f7cc53c0430783107e9062451a177bd7884c2
      (head (state-action-v1 mm-declare-disjoint 0 (state-require-each-v1 mm-symbol mm-state-active-variable (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule answer-fact-5ad04992ad368194f2b9d90ac0b42358e98b9c2f07d2d4521ac6e57bf8a4b00b
      (head (state-action-v1 mm-declare-disjoint 1 (state-insert-unordered-pairs-v1 mm-symbol mm-state-disjoint-variable state-insert-or-require-equal-v1)))
      (body))
    (rule answer-fact-95d6e5fccdb20999fd14c8ff1c7869699118051337a99fc5fb55bb98247c9834
      (head (state-action-v1 mm-declare-essential 0 (state-require-row-key-absent-v1 mm-state-symbol-kind (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-376a3bd92ce381da911822175cc61f01602ebdcd180ab4e48410c917b35d6041
      (head (state-action-v1 mm-declare-essential 1 (state-require-row-key-absent-v1 mm-state-label-name (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-81d6897b3a40f53bcbb155a4168060bbd046b7bda4ec71a271acd5100deb202d
      (head (state-action-v1 mm-declare-essential 2 (state-require-row-v1 mm-state-symbol-kind (state-row-cons-v1 (state-role-at-v1 mm-symbol 0) (state-row-cons-v1 (state-literal-v1 mm-symbol-constant) state-row-nil-v1)))))
      (body))
    (rule answer-fact-0e158db7bec66a0566d2b7dad74989f3645ecfb40e45ff41a8f8d6b22294dfdf
      (head (state-action-v1 mm-declare-essential 3 (state-require-each-v1 mm-symbol mm-state-expression-symbol (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule answer-fact-b7ae6d16f6256029108a3b168764d3a3fb84d2e49dd5ceccb645c121f09fc51c
      (head (state-action-v1 mm-declare-essential 4 (state-insert-row-v1 mm-state-active-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-literal-v1 mm-label-essential) (state-row-cons-v1 (state-literal-v1 mm-no-variable) (state-row-cons-v1 (state-role-list-v1 mm-symbol 0) state-row-nil-v1)))))))
      (body))
    (rule answer-fact-07986783ebe5506aa153804cc1cd0d9120347443ce56bd1203153d1fdb998dd2
      (head (state-action-v1 mm-declare-essential 5 (state-insert-row-v1 mm-state-formula state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-role-list-v1 mm-symbol 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-a4a5f1c91c16246e81fbdc007350ea054e1b2074dc9b8a245bc87e3f8f6304c1
      (head (state-action-v1 mm-declare-essential 6 (state-insert-each-matching-v1 mm-symbol mm-state-symbol-kind (state-row-cons-v1 state-input-v1 (state-row-cons-v1 (state-literal-v1 mm-symbol-variable) state-row-nil-v1)) mm-state-active-essential-variable state-insert-or-require-equal-v1 (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule answer-fact-cc8f5e8a90aa4b81a3e07a7780acab6fdde8e1bc52974fea5ec5e74c0aeb6903
      (head (state-action-v1 mm-declare-essential 7 (state-insert-row-v1 mm-state-label-name state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-0c8627531d597f8b2bfe7dd3990063ddf5403d32c4efc57a71bffba55cc99c45
      (head (state-action-v1 mm-declare-essential 8 (state-insert-row-v1 mm-state-label-kind state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-literal-v1 mm-label-essential) state-row-nil-v1)))))
      (body))
    (rule answer-fact-82fb772a9c067d0fc509997ad439f6a4517dc4478d67dec30c27aec9c31f168c
      (head (state-action-v1 mm-declare-floating 0 (state-require-row-key-absent-v1 mm-state-symbol-kind (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-5c76751aff57e8c53447795db01c22fe96f694fde409fee23ebd60ea2fc53336
      (head (state-action-v1 mm-declare-floating 1 (state-require-row-key-absent-v1 mm-state-label-name (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-61d5b162c3048959a1ea88c76ad5aacc36cd25399078a58772e21c95bc80a06c
      (head (state-action-v1 mm-declare-floating 10 (state-insert-row-v1 mm-state-formula state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-role-list-v1 mm-symbol 0) state-row-nil-v1)))))
      (body))
    (rule answer-fact-c7e0a13e8a4782f9e6da640ac1f4a7b6dcb7b217f1ebc21f9d742003f2765cda
      (head (state-action-v1 mm-declare-floating 2 (state-require-row-v1 mm-state-symbol-kind (state-row-cons-v1 (state-role-at-v1 mm-symbol 0) (state-row-cons-v1 (state-literal-v1 mm-symbol-constant) state-row-nil-v1)))))
      (body))
    (rule answer-fact-407f4065f9dd8a68b0c718d328fea95618209be6a4e9503fdeb6f7f39d88e589
      (head (state-action-v1 mm-declare-floating 3 (state-require-row-v1 mm-state-active-variable (state-row-cons-v1 (state-role-at-v1 mm-symbol 1) state-row-nil-v1))))
      (body))
    (rule answer-fact-558d9e4336b3ac239dff220834b87c8c86448696ba42382a214b277ad7cefcea
      (head (state-action-v1 mm-declare-floating 4 (state-insert-row-v1 mm-state-active-floating-variable state-require-key-absent-v1 (state-row-cons-v1 (state-role-at-v1 mm-symbol 1) state-row-nil-v1))))
      (body))
    (rule answer-fact-b522482c755af073b1da9c4914a11277a10499fb47203a501479eae6087a1fc9
      (head (state-action-v1 mm-declare-floating 5 (state-insert-row-v1 mm-state-active-hypothesis state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-literal-v1 mm-label-floating) (state-row-cons-v1 (state-role-at-v1 mm-symbol 1) (state-row-cons-v1 (state-role-list-v1 mm-symbol 0) state-row-nil-v1)))))))
      (body))
    (rule answer-fact-9f34bf21bafd0ef852be1f62b37baa1fa6e45bbfaf09439d40e5a50e64a6e669
      (head (state-action-v1 mm-declare-floating 6 (state-insert-row-v1 mm-state-expression-symbol state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-symbol 1) state-row-nil-v1))))
      (body))
    (rule answer-fact-c65ef3c394e12f53a234b6d99a89832e4d9244a992c550da4bb2fc8856dadbd4
      (head (state-action-v1 mm-declare-floating 7 (state-insert-row-v1 mm-state-label-name state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) state-row-nil-v1))))
      (body))
    (rule answer-fact-08fd261e8c3309ce6ef79306fdc60a559b4bc153d104d12a9bcc9696c18759a8
      (head (state-action-v1 mm-declare-floating 8 (state-insert-row-v1 mm-state-label-kind state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-literal-v1 mm-label-floating) state-row-nil-v1)))))
      (body))
    (rule answer-fact-01fd8397587227bf16371f84a653ddb372ab08a5aedf63aee1782075febff089
      (head (state-action-v1 mm-declare-floating 9 (state-insert-row-v1 mm-state-floating-variable state-insert-or-require-equal-v1 (state-row-cons-v1 (state-role-at-v1 mm-label 0) (state-row-cons-v1 (state-role-at-v1 mm-symbol 1) state-row-nil-v1)))))
      (body))
    (rule answer-fact-cbf67a9baabe9d1b80bf120d90a33dd77390c03c5f4c317b8930c7eebf80a178
      (head (state-action-v1 mm-declare-variables 0 (state-require-each-key-absent-v1 mm-symbol mm-state-label-name (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule answer-fact-1f8a69ebc5b4e81813b01e47fb25c0c4757e17938401e16eede160004c76ca3a
      (head (state-action-v1 mm-declare-variables 1 (state-insert-each-v1 mm-symbol mm-state-symbol-kind state-insert-or-require-equal-v1 (state-row-cons-v1 state-input-v1 (state-row-cons-v1 (state-literal-v1 mm-symbol-variable) state-row-nil-v1)))))
      (body))
    (rule answer-fact-0f1f943e4b8f37743abaf4e6c334198c65c042b66b6b2f9b7e05ab7bf7cdaa6f
      (head (state-action-v1 mm-declare-variables 2 (state-insert-each-v1 mm-symbol mm-state-active-variable state-require-key-absent-v1 (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule answer-fact-e38cff28b114e044836ae1eb144273759631e0b11b70736a29ef3a6450b22c6d
      (head (state-action-v1 mm-open-scope 0 state-push-scope-v1))
      (body))
    (rule answer-fact-50e277d8344a223899cb589309a218842134535d4fdae0be198b610ae1c245eb
      (head (state-action-v1 mm-resolve-include 0 (state-resolve-source-v1 mm-include-path state-skip-completed-source-v1 state-skip-active-source-v1)))
      (body))
    (rule answer-fact-aa6a021681fb1b75dc0570038272ce88ef120e1473c5d1e6fe6a9cda5feb446b
      (head (state-proof-machine-v1 mm-stack-proof-machine-v1 (state-stack-proof-machine-v1 (state-proof-tables-v1 mm-state-label-kind mm-state-formula mm-state-floating-variable mm-state-assertion-mandatory-variable mm-state-assertion-ordered-hypothesis mm-state-assertion-disjoint-variable mm-state-active-hypothesis mm-state-disjoint-variable mm-state-symbol-kind) (state-proof-kinds-v1 (state-literal-v1 mm-label-floating) (state-literal-v1 mm-label-essential) (state-literal-v1 mm-label-axiom) (state-literal-v1 mm-label-theorem) (state-literal-v1 mm-symbol-variable) (state-byte-literal-v1 63)) (state-proof-decoder-v1 65 84 85 89 90 63 20 0 5 1 state-proof-unknown-push-claim-v1 state-proof-save-immediately-after-use-v1 state-proof-header-nonmandatory-only-v1))))
      (body))
    (rule answer-fact-b2abdda4c5b5b4f7dfe328013f133e310d07a2aa27cc0de8746d2641be9ece99
      (head (state-table-v1 mm-state-active-essential-variable 1 1 state-scoped-v1))
      (body))
    (rule answer-fact-5fdd1a275a86f6d8a7f79e2b1222f96dae038734c74994442ff5c42ebc72eb94
      (head (state-table-v1 mm-state-active-floating-variable 1 1 state-scoped-v1))
      (body))
    (rule answer-fact-dc30ab3c238f345a4c6d4ae6499e87d5f945647b2db26a4d4b3d7718e6e0d88a
      (head (state-table-v1 mm-state-active-hypothesis 4 1 state-scoped-v1))
      (body))
    (rule answer-fact-81feec1bae3b651920d81013c71fac4c62b971e9d9beef63f1544e98399cc6d5
      (head (state-table-v1 mm-state-active-variable 1 1 state-scoped-v1))
      (body))
    (rule answer-fact-73075e866dcc083bad4b3ed217173c435b6ffab7b9f6aaae589cd5035e03856a
      (head (state-table-v1 mm-state-assertion-active-hypothesis 2 2 state-persistent-v1))
      (body))
    (rule answer-fact-11a94bb34be1f38e17dbaa93df6a8e91e3f20f4e9c0735162ff96fae3caeeef5
      (head (state-table-v1 mm-state-assertion-disjoint-left-candidate 3 3 state-transactional-v1))
      (body))
    (rule answer-fact-dd12ba76f21842abe19e768bc8ed6f40c64c99fe6dcfa31b17cb819c7c20a49c
      (head (state-table-v1 mm-state-assertion-disjoint-variable 3 3 state-persistent-v1))
      (body))
    (rule answer-fact-ad873121c775baeb73d8c8dda51f19ea155d96f3a6f4e91a052eca37631b625a
      (head (state-table-v1 mm-state-assertion-frame-hypothesis 2 2 state-persistent-v1))
      (body))
    (rule answer-fact-e875281271cb199ea73fda928774eab6c69355070bf39199f26c857bd9f85616
      (head (state-table-v1 mm-state-assertion-mandatory-variable 2 2 state-persistent-v1))
      (body))
    (rule answer-fact-63d57fa14939a52e51303e7447423bcf1c020fdb85e6a6cad6c73b657f6161f5
      (head (state-table-v1 mm-state-assertion-ordered-disjoint-variable 4 2 state-persistent-v1))
      (body))
    (rule answer-fact-06d6f0442b9b3f33322527bcb7b2b2c40cfd8dfb3ac9382b54946fd833520f60
      (head (state-table-v1 mm-state-assertion-ordered-frame-hypothesis 3 2 state-persistent-v1))
      (body))
    (rule answer-fact-2147a564063a048c9d378335e58c7d5bb6337f0ca819bf7aec44d6493d2c6abc
      (head (state-table-v1 mm-state-assertion-ordered-hypothesis 3 2 state-persistent-v1))
      (body))
    (rule answer-fact-70ee42a3a19ab034126f57527c4ee9a92a5179dffb35dbfef5b6e6182a0a3f43
      (head (state-table-v1 mm-state-disjoint-variable 2 2 state-scoped-v1))
      (body))
    (rule answer-fact-85f5b8e7988bf95ef34339cd2e45baf6f7e21cdc68d4b8a977afe7041d7d913d
      (head (state-table-v1 mm-state-expression-symbol 1 1 state-scoped-v1))
      (body))
    (rule answer-fact-aaed0a25f6ae8f871ae86a59af3ea1dbf7912f54601559927ce04e5fa1961dc5
      (head (state-table-v1 mm-state-floating-variable 2 1 state-persistent-v1))
      (body))
    (rule answer-fact-4694ca0af29a31fbe787d813ec3e51b75f4ddd43e444807e1d1f072bdadeb746
      (head (state-table-v1 mm-state-formula 2 1 state-persistent-v1))
      (body))
    (rule answer-fact-98be8e6eb0b22bb8361757493d05545ee87e3f21343746813f7a0a406baeca3b
      (head (state-table-v1 mm-state-label-kind 2 1 state-persistent-v1))
      (body))
    (rule answer-fact-6ad5d86a4eac0722aa48bfea4c2dfd683e077892bf17e87bd92103f6819cc2d9
      (head (state-table-v1 mm-state-label-name 1 1 state-persistent-v1))
      (body))
    (rule answer-fact-3abf3c441748a83aec78ac38cdd5ae3bd0e547fbeeec6001a6cb26482ab5aae7
      (head (state-table-v1 mm-state-symbol-kind 2 1 state-persistent-v1))
      (body))
  ))
