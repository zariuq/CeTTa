; generated finite-Horn facts from a canonical answer stream
(gslt-presentation-v1 GeneratedAnswerFactsV1_dd45e9681220d0ec5c6127fc21a3d2185f4f2bf7995331bca934daf8f994bd0c
  (signature
    (operator compile-oslf-native-type-v1 1)
    (operator oslf-carrier-v1 1)
    (operator oslf-external-relation-v1 3)
    (operator oslf-galois-v1 2)
    (operator oslf-head-signature-v1 2)
    (operator oslf-modal-v1 2)
    (operator oslf-native-former-v1 2)
    (operator oslf-step-schema-v1 4)
    (operator oslf-transition-semantics-v1 1)
    (operator proof-apply-v1 2)
    (operator proof-artifact-v1 1)
    (operator proof-calculus-constructor-v1 3)
    (operator proof-extension-v1 2)
    (operator proof-formal-v1 2)
    (operator proof-fvar-v1 1)
    (operator proof-judgment-v1 3)
    (operator proof-list-cons-v1 2)
    (operator proof-relational-projection-artifact-v1 1)
    (operator proof-relational-projection-identity-v1 2)
    (operator proof-relational-projection-selector-v1 3)
    (operator proof-relational-projection-table-v1 5)
    (operator proof-relational-runtime-artifact-v1 1)
    (operator proof-relational-runtime-binary-table-enumeration-v1 8)
    (operator proof-relational-runtime-compressed-input-v1 11)
    (operator proof-relational-runtime-control-literal-v1 4)
    (operator proof-relational-runtime-distinct-pairs-v1 4)
    (operator proof-relational-runtime-identity-v1 2)
    (operator proof-relational-runtime-input-byte-v1 4)
    (operator proof-relational-runtime-normal-input-v1 5)
    (operator proof-relational-runtime-ordinal-column-v1 5)
    (operator proof-relational-runtime-outcome-query-v1 5)
    (operator proof-relational-runtime-prefix-count-v1 7)
    (operator proof-relational-runtime-request-v1 3)
    (operator proof-relational-runtime-state-table-v1 5)
    (operator proof-relational-runtime-value-list-v1 4)
    (operator proof-rule-v1 6)
    (operator proof-sequence-evidence-artifact-v1 1)
    (operator proof-sequence-evidence-assertion-constructor-role-v1 4)
    (operator proof-sequence-evidence-assertion-judgment-role-v1 3)
    (operator proof-sequence-evidence-assertion-rule-role-v1 3)
    (operator proof-sequence-evidence-constructor-role-v1 4)
    (operator proof-sequence-evidence-judgment-role-v1 3)
    (operator proof-sequence-evidence-rule-role-v1 3)
    (operator proof-sequence-relational-artifact-v1 1)
    (operator proof-sequence-relational-execution-v1 16)
    (operator proof-sequence-relational-identity-v1 2)
    (operator proof-sequence-relational-selector-v1 3)
    (operator proof-sequence-relational-table-policy-v1 3)
    (operator proof-sequence-relational-table-v1 5)
    (operator proof-succ-v1 1)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-int 1)
    (operator q-succ 1)
    (operator q-sym 1)
    (operator q-var 1)
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
    (operator state-proof-action-v1 3)
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
    (rule answer-fact-aa3854bd973532f6ba620809eb4ab38eaa1d2ea849cfb8c639978f12a4a79172
      (head (compile-oslf-native-type-v1 (oslf-carrier-v1 finite-horn-goal-state-v1)))
      (body))
    (rule answer-fact-044838224ff9d27746925f18b03f5a6b00ea3228b0da4384d931363f735bdf12
      (head (compile-oslf-native-type-v1 (oslf-external-relation-v1 (q-sym CertificateGSLTFirstOrderSemanticInterfaceV1) (q-sym ProofAssertionDeclaredV1) (q-int 1))))
      (body))
    (rule answer-fact-fbaced0a624665b0440ee5b4e600150a92c5afb0428bf2309fb20b7478e18899
      (head (compile-oslf-native-type-v1 (oslf-external-relation-v1 (q-sym CertificateGSLTFirstOrderSemanticInterfaceV1) (q-sym ProofTokenApartV1) (q-int 2))))
      (body))
    (rule answer-fact-dae7a126ed635e634f1ebd1e8164842ecc51b0543491f6823e70ceafdf29d057
      (head (compile-oslf-native-type-v1 (oslf-external-relation-v1 (q-sym CertificateGSLTFirstOrderSemanticInterfaceV1) (q-sym ProofTokenDifferentV1) (q-int 2))))
      (body))
    (rule answer-fact-0d561cfff9abdc6f74361c43c716ae5d9ceab5adaeb32c67ae41bbf5a87112c8
      (head (compile-oslf-native-type-v1 (oslf-external-relation-v1 (q-sym CertificateGSLTFirstOrderSemanticInterfaceV1) (q-sym ProofTokenLiteralV1) (q-int 1))))
      (body))
    (rule answer-fact-bca4398fa3794b7033689e163a112a3ce0e48488ce974d8cfeb9ef695f994c76
      (head (compile-oslf-native-type-v1 (oslf-external-relation-v1 (q-sym CertificateGSLTFirstOrderSemanticInterfaceV1) (q-sym ProofTokenVariableV1) (q-int 1))))
      (body))
    (rule answer-fact-69a9b402ac52f14404f78d03a9bb9f402d724f76abbdd1cb95deceb49f774bc3
      (head (compile-oslf-native-type-v1 (oslf-galois-v1 ntt-diamond-v1 ntt-box-v1)))
      (body))
    (rule answer-fact-ab7dd8992f8e48c3026172cc8030b13ff3ff9adf48d9987d784aa58a4ee08887
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 MetamathProvableV1 (q-succ q-zero))))
      (body))
    (rule answer-fact-eddcf3726d84a897e0e6e6e7faeac4dbfc526cd974ea3c0121566f4cf3a3a0be
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofAssertionBuildEnvironmentV1 (q-succ (q-succ (q-succ q-zero))))))
      (body))
    (rule answer-fact-853bec56ad742b34c2987e36dc13962838e98425b9783af9b4f90e6093227c97
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofAssertionCheckDisjointsV1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-5e5288d4fad0b6afbfcfa0280ad9b142b032dab913ba651a141eac28721125d5
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofAssertionCheckEssentialsV1 (q-succ (q-succ (q-succ q-zero))))))
      (body))
    (rule answer-fact-8b8e09581d9b7eea49a753ec2641aeaa5bc0b8fd39cc255ddd46146fca489acc
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofAssertionDeclaredV1 (q-succ q-zero))))
      (body))
    (rule answer-fact-663785134b830deae7c4b0f3dcdb1f9c5da1f7dea9fd4d13ac68356083b8082d
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofAssertionDisjointV1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-a627655f5df55860bf338105e38d545435c7a52d7bc2ba81b4ff7b9f96e45559
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofAssertionEssentialV1 (q-succ q-zero))))
      (body))
    (rule answer-fact-3e0a00608cf5e8b42bdba862b966f4db4454b5ee44b5e5d90a69f2445a9aa29d
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofAssertionListConsV1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-f96a0f0365a787a62de0c710d8ab6252354a35aecf7bde38e39400014a0a8cf6
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofAssertionListNilV1 q-zero)))
      (body))
    (rule answer-fact-bcb7731f5e987a10c9f8f95500011a17411b4aea2e4b80528e13396eadb56b57
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofAssertionV1 (q-succ (q-succ (q-succ (q-succ (q-succ q-zero))))))))
      (body))
    (rule answer-fact-ac589ffc7ebd40462acf06f7b6099eaa11bba8685fc2251855dd98e02445a651
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofAssertionVariableV1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-85a5cd629dbeb97a3db6e12fd022af270bb9f56c5b28bdd9f8eeb68c6e26a135
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofEnvironmentConsV1 (q-succ (q-succ (q-succ q-zero))))))
      (body))
    (rule answer-fact-2dbcb98b32b569646f6bcd524c35b9c83a8e7a3f9ada4e5f004ce727bdb248d8
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofEnvironmentLookupV1 (q-succ (q-succ (q-succ q-zero))))))
      (body))
    (rule answer-fact-f3f7c7b28ef5c5cf7c62a03fd4ee4283fb59e0bbe9f3bd1cee22727767a222df
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofEnvironmentNilV1 q-zero)))
      (body))
    (rule answer-fact-cb3dd08c8f3a301b38777afee6f07be8affaf0985839df5161e002880b003939
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofSequenceAppendV1 (q-succ (q-succ (q-succ q-zero))))))
      (body))
    (rule answer-fact-ebf92a0921d0f6c7dd660ef6ce5c8a190d7171cc749ad82075088775674f0e02
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofSequenceConsV1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-ce89bc39668f3868c24e736fc180ebf097233c95a934e684160a8a083a334a04
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofSequenceInstantiateV1 (q-succ (q-succ (q-succ q-zero))))))
      (body))
    (rule answer-fact-73af66b731c54846e40e2d8de0ed22ce64757cbae0f31a34142061876a149e88
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofSequenceNilV1 q-zero)))
      (body))
    (rule answer-fact-75f9a7666a118aeeff41291d11374c1c5cc81e87e433ba564af2f90c74bd5b7d
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofSequenceSupportApartV1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-43d65d94f4cb6859382b5dff1c48a4d0d6cf757c34a0a8950a71dde8b53c3cd9
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofTokenAgainstSequenceV1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-2ecf9aa8ecb5af5bd987e2e559b969d4b224e4df2ba5bfb43c05a04aa8bd354e
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofTokenApartV1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-5596992942ac84a5a07d792ed4426f58450cc7af1aff668ea0ef241a97bdf4b4
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofTokenDifferentV1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-c51b25a224883e8f3ed6b4c3b5284a9943a5ef4e1d08fb293194434e689387a7
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofTokenLiteralV1 (q-succ q-zero))))
      (body))
    (rule answer-fact-e7d601d860b4b5fe944c8f67ed41283a40eff56d6ce245449afa8ff5b11d3663
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofTokenPairAllowedV1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-be6ee9bf20925b442a6e321b9c606b04bc630d313e78e6a4c67b26c69a130f27
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 ProofTokenVariableV1 (q-succ q-zero))))
      (body))
    (rule answer-fact-4cdf9bf441b9447e6076d2ec5bc3a7c61d293c3d2771e9650ecb366e5abeaafc
      (head (compile-oslf-native-type-v1 (oslf-head-signature-v1 oslf-external-relation-decl-v1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-6ed60b76b745fd438f0773100506a42f0fe4f0fe4e9d7a94e0cf7a333e73c8cd
      (head (compile-oslf-native-type-v1 (oslf-modal-v1 ntt-box-v1 predecessor-universal-v1)))
      (body))
    (rule answer-fact-662b32e33e34030943a9e1f8dddde210566ffcd381d49035db2556594423dd53
      (head (compile-oslf-native-type-v1 (oslf-modal-v1 ntt-diamond-v1 forward-existential-v1)))
      (body))
    (rule answer-fact-c053bcf2013a57673b5a9bb6403d918700a58e3653e50da0c26f6223c40e7f75
      (head (compile-oslf-native-type-v1 (oslf-native-former-v1 ntt-bottom-v1 q-zero)))
      (body))
    (rule answer-fact-720afa8f76dcf5865e9f0f5c3383d74b0e3506134fa93517679c9e3b05b54dde
      (head (compile-oslf-native-type-v1 (oslf-native-former-v1 ntt-box-v1 (q-succ q-zero))))
      (body))
    (rule answer-fact-aa080a69384eea63f28c6066c631cb2ee2627efb9c8ef2306eb02a99aff07fbb
      (head (compile-oslf-native-type-v1 (oslf-native-former-v1 ntt-conjunction-v1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-12d50134f768fd43d7f8c36f6886d8fad18d59aee20dd5493feb78febd085219
      (head (compile-oslf-native-type-v1 (oslf-native-former-v1 ntt-diamond-v1 (q-succ q-zero))))
      (body))
    (rule answer-fact-fe2b5c2098d7d1d6664e21f6456b70a8955fc264236f08fe0662cdb83296dc5b
      (head (compile-oslf-native-type-v1 (oslf-native-former-v1 ntt-disjunction-v1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-2e845ef0c169a41487a5b3a7c2c7f880558641b4705243c424e6c552c97bfdd1
      (head (compile-oslf-native-type-v1 (oslf-native-former-v1 ntt-headed-v1 (q-succ (q-succ q-zero)))))
      (body))
    (rule answer-fact-6aa58ac3f9abb0046f4fec3b02253015509e17d5d8e9ff9f47453750fd63e289
      (head (compile-oslf-native-type-v1 (oslf-native-former-v1 ntt-top-v1 q-zero)))
      (body))
    (rule answer-fact-36db8eca7178a834f90257391c29969fd629ec137e4ab3bcdb0cf0ad145683de
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 (q-sym CertificateGSLTFirstOrderSemanticInterfaceV1) proof-first-order-interface-assertion-v1 (q-app (q-sym oslf-external-relation-decl-v1) (q-cons (q-sym ProofAssertionDeclaredV1) (q-cons (q-int 1) q-nil))) q-nil)))
      (body))
    (rule answer-fact-b9bb0699ddb7ae21ad76f9ba287b109090e07054eee8361e0d9d2485c2010389
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 (q-sym CertificateGSLTFirstOrderSemanticInterfaceV1) proof-first-order-interface-token-apart-v1 (q-app (q-sym oslf-external-relation-decl-v1) (q-cons (q-sym ProofTokenApartV1) (q-cons (q-int 2) q-nil))) q-nil)))
      (body))
    (rule answer-fact-82de927be38378381797927d63f4dc7dc16a681874446b246a13dd7bf40a3bfa
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 (q-sym CertificateGSLTFirstOrderSemanticInterfaceV1) proof-first-order-interface-token-different-v1 (q-app (q-sym oslf-external-relation-decl-v1) (q-cons (q-sym ProofTokenDifferentV1) (q-cons (q-int 2) q-nil))) q-nil)))
      (body))
    (rule answer-fact-335787db41e37dc76ff9c77a14f1ef53ddfe68391e497dfebddf9289d0a9c669
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 (q-sym CertificateGSLTFirstOrderSemanticInterfaceV1) proof-first-order-interface-token-literal-v1 (q-app (q-sym oslf-external-relation-decl-v1) (q-cons (q-sym ProofTokenLiteralV1) (q-cons (q-int 1) q-nil))) q-nil)))
      (body))
    (rule answer-fact-b2700a169f414455161c5ae710af23a0665cebb00dc064f646b050b473d25d01
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 (q-sym CertificateGSLTFirstOrderSemanticInterfaceV1) proof-first-order-interface-token-variable-v1 (q-app (q-sym oslf-external-relation-decl-v1) (q-cons (q-sym ProofTokenVariableV1) (q-cons (q-int 1) q-nil))) q-nil)))
      (body))
    (rule answer-fact-c7146463e1438317114285390f4ab34969796ff0939b08bcacac5126698bf71d
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-assertion-apply-v1 (q-app (q-sym MetamathProvableV1) (q-cons (q-app (q-sym ProofSequenceConsV1) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ q-zero))))))))) q-nil))) q-nil)) (q-cons (q-app (q-sym ProofAssertionDeclaredV1) (q-cons (q-app (q-sym ProofAssertionV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) q-nil)))))) q-nil)) (q-cons (q-app (q-sym ProofAssertionBuildEnvironmentV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))))) q-nil)))) (q-cons (q-app (q-sym ProofAssertionCheckEssentialsV1) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ q-zero))))))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))))) q-nil)))) (q-cons (q-app (q-sym ProofAssertionCheckDisjointsV1) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))))) q-nil))) (q-cons (q-app (q-sym ProofSequenceInstantiateV1) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ q-zero))))))))) q-nil)))) q-nil))))))))
      (body))
    (rule answer-fact-a12b59f667a0881f4373ee73404185a3aacb191d41344a87380eb0d3b0ef5ad3
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-assertion-build-environment-cons-v1 (q-app (q-sym ProofAssertionBuildEnvironmentV1) (q-cons (q-app (q-sym ProofAssertionListConsV1) (q-cons (q-app (q-sym ProofAssertionVariableV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) q-nil))) (q-cons (q-app (q-sym ProofAssertionListConsV1) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) q-nil))) (q-cons (q-app (q-sym ProofEnvironmentConsV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))) q-nil)))) q-nil)))) (q-cons (q-app (q-sym ProofTokenVariableV1) (q-cons (q-var q-zero) q-nil)) (q-cons (q-app (q-sym MetamathProvableV1) (q-cons (q-app (q-sym ProofSequenceConsV1) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ q-zero))) q-nil))) q-nil)) (q-cons (q-app (q-sym ProofAssertionBuildEnvironmentV1) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))) q-nil)))) q-nil))))))
      (body))
    (rule answer-fact-80f30def559e5ab0e47fc4e87e9c1a801b707bc81986f218028c3b57b7a6feec
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-assertion-build-environment-nil-v1 (q-app (q-sym ProofAssertionBuildEnvironmentV1) (q-cons (q-app (q-sym ProofAssertionListNilV1) q-nil) (q-cons (q-app (q-sym ProofAssertionListNilV1) q-nil) (q-cons (q-app (q-sym ProofEnvironmentNilV1) q-nil) q-nil)))) q-nil)))
      (body))
    (rule answer-fact-45eaf47ce33729c3685e60faa09087b7cea16a9a9f175749df6fd6000bb80ced
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-assertion-check-disjoints-cons-v1 (q-app (q-sym ProofAssertionCheckDisjointsV1) (q-cons (q-app (q-sym ProofAssertionListConsV1) (q-cons (q-app (q-sym ProofAssertionDisjointV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-var (q-succ (q-succ q-zero))) q-nil))) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) q-nil))) (q-cons (q-app (q-sym ProofTokenDifferentV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-app (q-sym ProofEnvironmentLookupV1) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) (q-cons (q-var q-zero) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) q-nil)))) (q-cons (q-app (q-sym ProofEnvironmentLookupV1) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))) q-nil)))) (q-cons (q-app (q-sym ProofSequenceSupportApartV1) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))) q-nil))) (q-cons (q-app (q-sym ProofAssertionCheckDisjointsV1) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) q-nil))) q-nil))))))))
      (body))
    (rule answer-fact-e5f2ba68b44ef6a18d13b6c4e7000cb0a230f5bbf073f52b37e09aad821f798e
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-assertion-check-disjoints-nil-v1 (q-app (q-sym ProofAssertionCheckDisjointsV1) (q-cons (q-app (q-sym ProofAssertionListNilV1) q-nil) (q-cons (q-var q-zero) q-nil))) q-nil)))
      (body))
    (rule answer-fact-a6e47f885769a9a0404b408a7fb800604010a54614965715bac25823095e399a
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-assertion-check-essentials-cons-v1 (q-app (q-sym ProofAssertionCheckEssentialsV1) (q-cons (q-app (q-sym ProofAssertionListConsV1) (q-cons (q-app (q-sym ProofAssertionEssentialV1) (q-cons (q-var q-zero) q-nil)) (q-cons (q-var (q-succ (q-succ q-zero))) q-nil))) (q-cons (q-app (q-sym ProofAssertionListConsV1) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) q-nil))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) q-nil)))) (q-cons (q-app (q-sym MetamathProvableV1) (q-cons (q-var (q-succ q-zero)) q-nil)) (q-cons (q-app (q-sym ProofSequenceInstantiateV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) (q-cons (q-var (q-succ q-zero)) q-nil)))) (q-cons (q-app (q-sym ProofAssertionCheckEssentialsV1) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) q-nil)))) q-nil))))))
      (body))
    (rule answer-fact-39965b28b49740d23014b5ea875f349a5f461ceb959aad64c27aad109d4417cc
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-assertion-check-essentials-nil-v1 (q-app (q-sym ProofAssertionCheckEssentialsV1) (q-cons (q-app (q-sym ProofAssertionListNilV1) q-nil) (q-cons (q-app (q-sym ProofAssertionListNilV1) q-nil) (q-cons (q-var q-zero) q-nil)))) q-nil)))
      (body))
    (rule answer-fact-208fb13b48ade246974a65711c5c75d9914da99569651ba9065061f1cdf689a2
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-assertion-use-premise-v1 (q-app (q-sym MetamathProvableV1) (q-cons (q-var q-zero) q-nil)) (q-cons (q-app (q-sym MetamathProvableV1) (q-cons (q-var q-zero) q-nil)) q-nil))))
      (body))
    (rule answer-fact-1e14ad7d2bc2cab157f5921f2955a4e46ea20a091bd024ef5fd62174ca5718ac
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-environment-lookup-head-v1 (q-app (q-sym ProofEnvironmentLookupV1) (q-cons (q-app (q-sym ProofEnvironmentConsV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ q-zero))) q-nil)))) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil)))) q-nil)))
      (body))
    (rule answer-fact-3683253d173dba8b4f4029f630f9f90e74352402149c9cd1880b7b617ff0d5a8
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-environment-lookup-tail-v1 (q-app (q-sym ProofEnvironmentLookupV1) (q-cons (q-app (q-sym ProofEnvironmentConsV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ q-zero))) q-nil)))) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) q-nil)))) (q-cons (q-app (q-sym ProofTokenDifferentV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) q-nil))) (q-cons (q-app (q-sym ProofEnvironmentLookupV1) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) q-nil)))) q-nil)))))
      (body))
    (rule answer-fact-4ebf2b348380d4d49e4f5824c301f36ffe75877b712e0f8fb5bb287c451dcfed
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-sequence-append-cons-v1 (q-app (q-sym ProofSequenceAppendV1) (q-cons (q-app (q-sym ProofSequenceConsV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-app (q-sym ProofSequenceConsV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) q-nil))) q-nil)))) (q-cons (q-app (q-sym ProofSequenceAppendV1) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) q-nil)))) q-nil))))
      (body))
    (rule answer-fact-4bf9a2a21eccfcb522599563acea84a463200f76acdba1d70520c293a898e992
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-sequence-append-nil-v1 (q-app (q-sym ProofSequenceAppendV1) (q-cons (q-app (q-sym ProofSequenceNilV1) q-nil) (q-cons (q-var q-zero) (q-cons (q-var q-zero) q-nil)))) q-nil)))
      (body))
    (rule answer-fact-9dd726719732450e977cb33a65e259704e3cffa12c53a9623d8b6cff0f7ea5a8
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-sequence-instantiate-literal-v1 (q-app (q-sym ProofSequenceInstantiateV1) (q-cons (q-app (q-sym ProofSequenceConsV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-app (q-sym ProofSequenceConsV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) q-nil))) q-nil)))) (q-cons (q-app (q-sym ProofTokenLiteralV1) (q-cons (q-var q-zero) q-nil)) (q-cons (q-app (q-sym ProofSequenceInstantiateV1) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) q-nil)))) q-nil)))))
      (body))
    (rule answer-fact-2ef997ecdaec96dbe70f140e5d0db8f4ff2d0fbdb432182520d5bf5c94a8449c
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-sequence-instantiate-nil-v1 (q-app (q-sym ProofSequenceInstantiateV1) (q-cons (q-app (q-sym ProofSequenceNilV1) q-nil) (q-cons (q-var q-zero) (q-cons (q-app (q-sym ProofSequenceNilV1) q-nil) q-nil)))) q-nil)))
      (body))
    (rule answer-fact-72a567dbd6e2018726e3046b8f151236cfd5bfc4be0030d61504b5fd7ae688f7
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-sequence-instantiate-variable-v1 (q-app (q-sym ProofSequenceInstantiateV1) (q-cons (q-app (q-sym ProofSequenceConsV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))) q-nil)))) (q-cons (q-app (q-sym ProofTokenVariableV1) (q-cons (q-var q-zero) q-nil)) (q-cons (q-app (q-sym ProofEnvironmentLookupV1) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var q-zero) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) q-nil)))) (q-cons (q-app (q-sym ProofSequenceInstantiateV1) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ q-zero))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) q-nil)))) (q-cons (q-app (q-sym ProofSequenceAppendV1) (q-cons (q-var (q-succ (q-succ (q-succ q-zero)))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ q-zero))))) (q-cons (q-var (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))) q-nil)))) q-nil)))))))
      (body))
    (rule answer-fact-2b789cec0441ce8b048cf4321d29e4b533584f2c703aa48f39e4848d2de187e3
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-sequence-support-apart-cons-v1 (q-app (q-sym ProofSequenceSupportApartV1) (q-cons (q-app (q-sym ProofSequenceConsV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-var (q-succ (q-succ q-zero))) q-nil))) (q-cons (q-app (q-sym ProofTokenAgainstSequenceV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ (q-succ q-zero))) q-nil))) (q-cons (q-app (q-sym ProofSequenceSupportApartV1) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ q-zero))) q-nil))) q-nil)))))
      (body))
    (rule answer-fact-d69b8b5826eba2673bf659e65045ca10817181e8407edacae2f332cb0d0e3a8f
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-sequence-support-apart-nil-v1 (q-app (q-sym ProofSequenceSupportApartV1) (q-cons (q-app (q-sym ProofSequenceNilV1) q-nil) (q-cons (q-var q-zero) q-nil))) q-nil)))
      (body))
    (rule answer-fact-2935043f624485eb7e42068c58e5178b1f1ac0d370e17c9b342329c5ca9d5fef
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-token-against-sequence-cons-v1 (q-app (q-sym ProofTokenAgainstSequenceV1) (q-cons (q-var q-zero) (q-cons (q-app (q-sym ProofSequenceConsV1) (q-cons (q-var (q-succ q-zero)) (q-cons (q-var (q-succ (q-succ q-zero))) q-nil))) q-nil))) (q-cons (q-app (q-sym ProofTokenPairAllowedV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-app (q-sym ProofTokenAgainstSequenceV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ (q-succ q-zero))) q-nil))) q-nil)))))
      (body))
    (rule answer-fact-f846aa1cd28053653eee7963a0d646298461e75ed98649f462c5912feb12055f
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-token-against-sequence-nil-v1 (q-app (q-sym ProofTokenAgainstSequenceV1) (q-cons (q-var q-zero) (q-cons (q-app (q-sym ProofSequenceNilV1) q-nil) q-nil))) q-nil)))
      (body))
    (rule answer-fact-8737e3e39493afef0012cde4de178fc24a00c0d5842ef74e61c349b2272fa0e3
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-token-pair-apart-v1 (q-app (q-sym ProofTokenPairAllowedV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-app (q-sym ProofTokenVariableV1) (q-cons (q-var q-zero) q-nil)) (q-cons (q-app (q-sym ProofTokenVariableV1) (q-cons (q-var (q-succ q-zero)) q-nil)) (q-cons (q-app (q-sym ProofTokenApartV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) q-nil))))))
      (body))
    (rule answer-fact-ec6cf5849f835cdd1495dcc6d658a849908b505cb86737c75db50e0a70dfd729
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-token-pair-left-literal-v1 (q-app (q-sym ProofTokenPairAllowedV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-app (q-sym ProofTokenLiteralV1) (q-cons (q-var q-zero) q-nil)) q-nil))))
      (body))
    (rule answer-fact-3403b7df7b697347dd17d26da29925147f50b22a6259f5e2e9495619294fcbd9
      (head (compile-oslf-native-type-v1 (oslf-step-schema-v1 MetamathProofV1 proof-token-pair-right-literal-v1 (q-app (q-sym ProofTokenPairAllowedV1) (q-cons (q-var q-zero) (q-cons (q-var (q-succ q-zero)) q-nil))) (q-cons (q-app (q-sym ProofTokenLiteralV1) (q-cons (q-var (q-succ q-zero)) q-nil)) q-nil))))
      (body))
    (rule answer-fact-a965ac6c779e533b53cf4a4f9f2eb39e0c0097f0a3a92d6356e29c9a4eceb7f9
      (head (compile-oslf-native-type-v1 (oslf-transition-semantics-v1 finite-horn-backward-goal-reduction-v1)))
      (body))
    (rule answer-fact-88b3905a507e8457d3d52d5cfb2e52a6870537c43ff741039bd77f742d53fbf6
      (head (proof-artifact-v1 (proof-calculus-constructor-v1 MetamathProofV1 ProofAssertionDisjointV1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule answer-fact-923e7e5e64b25af3ee4defa58b341fd9e966d1400beab437c549295caa466ad5
      (head (proof-artifact-v1 (proof-calculus-constructor-v1 MetamathProofV1 ProofAssertionEssentialV1 (proof-succ-v1 proof-zero-v1))))
      (body))
    (rule answer-fact-1eb66dfbe1c005b34de34797dc4794b7e1169899d9c772134ecc892ccce7f99a
      (head (proof-artifact-v1 (proof-calculus-constructor-v1 MetamathProofV1 ProofAssertionListConsV1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule answer-fact-085ffb812f0104c79d5f119f4388263e860261929a76bef46ed3e6f975027562
      (head (proof-artifact-v1 (proof-calculus-constructor-v1 MetamathProofV1 ProofAssertionListNilV1 proof-zero-v1)))
      (body))
    (rule answer-fact-241a4dbaf918866b156d930799f4922c29336a799beeb303169285eabf761d5a
      (head (proof-artifact-v1 (proof-calculus-constructor-v1 MetamathProofV1 ProofAssertionV1 (proof-succ-v1 (proof-succ-v1 (proof-succ-v1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))))))
      (body))
    (rule answer-fact-ff917f981e51dae4f12146226882b4a38c007a499301c7dac8a939f111d39e3c
      (head (proof-artifact-v1 (proof-calculus-constructor-v1 MetamathProofV1 ProofAssertionVariableV1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule answer-fact-ead9f53ac3f1e0ee50d7b4ace5c047156fd9e36ab74ce0d8982e08393c6ed7d6
      (head (proof-artifact-v1 (proof-calculus-constructor-v1 MetamathProofV1 ProofEnvironmentConsV1 (proof-succ-v1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))))
      (body))
    (rule answer-fact-413313ab2c2a679ad175ebc32b3e4a48360cef02a4d5f4684ba6e3bee6daff3b
      (head (proof-artifact-v1 (proof-calculus-constructor-v1 MetamathProofV1 ProofEnvironmentNilV1 proof-zero-v1)))
      (body))
    (rule answer-fact-77e1ec618e881c6b25d02186c0c7b50e015748be9ba3725c015844f6f644be26
      (head (proof-artifact-v1 (proof-calculus-constructor-v1 MetamathProofV1 ProofSequenceConsV1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule answer-fact-9c153506077a3528e411a68489287524d6ef9f4c8351c23a7afb02ba6aa749f8
      (head (proof-artifact-v1 (proof-calculus-constructor-v1 MetamathProofV1 ProofSequenceNilV1 proof-zero-v1)))
      (body))
    (rule answer-fact-ef58ee38def441a14005217147a0aa44492346734e1e8deebead9a3a5dc21b85
      (head (proof-artifact-v1 (proof-extension-v1 MetamathProofV1 MetamathLanguageV1)))
      (body))
    (rule answer-fact-dae83d7b81c6afc6f615e939999df252a925a98bd374de030ffb01ccf631688f
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 MetamathProvableV1 (proof-succ-v1 proof-zero-v1))))
      (body))
    (rule answer-fact-83cd5c7244b0c0ba3bb02667c14f0d05243b31966f4f884fa1a3ded548a16c89
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofAssertionBuildEnvironmentV1 (proof-succ-v1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))))
      (body))
    (rule answer-fact-a1a061c60df479a660c9bb124afe74347f9ddf96ab229fb6d8e356f2d2622bdd
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofAssertionCheckDisjointsV1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule answer-fact-cf5882db7cdc9dd45fea27ed70d9a310605d67b7ab03db5b1521c6d16f43c882
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofAssertionCheckEssentialsV1 (proof-succ-v1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))))
      (body))
    (rule answer-fact-b9e76bc334b40214b04bcb545e51cc94af1e8897b8b8b5b6a27dca3349df7fa8
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofAssertionDeclaredV1 (proof-succ-v1 proof-zero-v1))))
      (body))
    (rule answer-fact-aa5b10a2d9b2cabb9111f77099a133f5226bc59035b753c7384c8be257221301
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofEnvironmentLookupV1 (proof-succ-v1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))))
      (body))
    (rule answer-fact-2ebcb406d8a6e5a37ad341269c2897796e73f715317c0472e1def3de0ceb81d6
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofSequenceAppendV1 (proof-succ-v1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))))
      (body))
    (rule answer-fact-74fccb4dcea9a52b6973fd869bd0c9b5df0c73408e5741deaee185e7b41ed79e
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofSequenceInstantiateV1 (proof-succ-v1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))))
      (body))
    (rule answer-fact-dbe1b49f6a460664ca3201e01507dcaeacb2f4722fe57d4d23eaf745a38b948c
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofSequenceSupportApartV1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule answer-fact-1b1c3cf63790b6d3d7d5d5e42455267d97654742422d5c332d3ab9a25449af36
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofTokenAgainstSequenceV1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule answer-fact-2f12de44d017b453d6888f99e363106ee58dc9e2500ef86dd11b4c31c612fbef
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofTokenApartV1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule answer-fact-b7a9b0ef5b2f4a141259b96f6d9cc82cd61d36063b9b6416e9271e40de4ea8c3
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofTokenDifferentV1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule answer-fact-dd0a5a4ad0c7a7b0962d906c5e82c2de483c27d030b6726f5a8574a8cd035271
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofTokenLiteralV1 (proof-succ-v1 proof-zero-v1))))
      (body))
    (rule answer-fact-fe0ad40ca08e0f2702599c9f36c5484f4b0f43fcbe044623fcf8a3ca65c63f9a
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofTokenPairAllowedV1 (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body))
    (rule answer-fact-f654639f59c4a401cda03c8a5d289b8974b8ab13eb63ecb7d409e343fda59fd2
      (head (proof-artifact-v1 (proof-judgment-v1 MetamathProofV1 ProofTokenVariableV1 (proof-succ-v1 proof-zero-v1))))
      (body))
    (rule answer-fact-c2f99136ff47bbb78a9c4265109f17e6f201a39360cd8bee2b656a5347bd741e
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-assertion-apply-v1 (proof-list-cons-v1 (proof-formal-v1 Variables proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Essentials proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 ConclusionType proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 ConclusionTemplate proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Disjoints proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Images proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Actuals proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Environment proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Result proof-zero-v1) proof-list-nil-v1))))))))) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionDeclaredV1 (proof-list-cons-v1 (proof-apply-v1 ProofAssertionV1 (proof-list-cons-v1 (proof-fvar-v1 Variables) (proof-list-cons-v1 (proof-fvar-v1 Essentials) (proof-list-cons-v1 (proof-fvar-v1 ConclusionType) (proof-list-cons-v1 (proof-fvar-v1 ConclusionTemplate) (proof-list-cons-v1 (proof-fvar-v1 Disjoints) proof-list-nil-v1)))))) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionBuildEnvironmentV1 (proof-list-cons-v1 (proof-fvar-v1 Variables) (proof-list-cons-v1 (proof-fvar-v1 Images) (proof-list-cons-v1 (proof-fvar-v1 Environment) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionCheckEssentialsV1 (proof-list-cons-v1 (proof-fvar-v1 Essentials) (proof-list-cons-v1 (proof-fvar-v1 Actuals) (proof-list-cons-v1 (proof-fvar-v1 Environment) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionCheckDisjointsV1 (proof-list-cons-v1 (proof-fvar-v1 Disjoints) (proof-list-cons-v1 (proof-fvar-v1 Environment) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceInstantiateV1 (proof-list-cons-v1 (proof-fvar-v1 ConclusionTemplate) (proof-list-cons-v1 (proof-fvar-v1 Environment) (proof-list-cons-v1 (proof-fvar-v1 Result) proof-list-nil-v1)))) proof-list-nil-v1))))) (proof-apply-v1 MetamathProvableV1 (proof-list-cons-v1 (proof-apply-v1 ProofSequenceConsV1 (proof-list-cons-v1 (proof-fvar-v1 ConclusionType) (proof-list-cons-v1 (proof-fvar-v1 Result) proof-list-nil-v1))) proof-list-nil-v1)) proof-list-nil-v1)))
      (body))
    (rule answer-fact-ba1237ed02573c4694f2f3e2c7cb7e990a4c5501928688b39134f045d072a065
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-assertion-build-environment-cons-v1 (proof-list-cons-v1 (proof-formal-v1 Variable proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Typecode proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Image proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 VariablesRest proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 ImagesRest proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 EnvironmentRest proof-zero-v1) proof-list-nil-v1)))))) (proof-list-cons-v1 (proof-apply-v1 ProofTokenVariableV1 (proof-list-cons-v1 (proof-fvar-v1 Variable) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 MetamathProvableV1 (proof-list-cons-v1 (proof-apply-v1 ProofSequenceConsV1 (proof-list-cons-v1 (proof-fvar-v1 Typecode) (proof-list-cons-v1 (proof-fvar-v1 Image) proof-list-nil-v1))) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionBuildEnvironmentV1 (proof-list-cons-v1 (proof-fvar-v1 VariablesRest) (proof-list-cons-v1 (proof-fvar-v1 ImagesRest) (proof-list-cons-v1 (proof-fvar-v1 EnvironmentRest) proof-list-nil-v1)))) proof-list-nil-v1))) (proof-apply-v1 ProofAssertionBuildEnvironmentV1 (proof-list-cons-v1 (proof-apply-v1 ProofAssertionListConsV1 (proof-list-cons-v1 (proof-apply-v1 ProofAssertionVariableV1 (proof-list-cons-v1 (proof-fvar-v1 Variable) (proof-list-cons-v1 (proof-fvar-v1 Typecode) proof-list-nil-v1))) (proof-list-cons-v1 (proof-fvar-v1 VariablesRest) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionListConsV1 (proof-list-cons-v1 (proof-fvar-v1 Image) (proof-list-cons-v1 (proof-fvar-v1 ImagesRest) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofEnvironmentConsV1 (proof-list-cons-v1 (proof-fvar-v1 Variable) (proof-list-cons-v1 (proof-fvar-v1 Image) (proof-list-cons-v1 (proof-fvar-v1 EnvironmentRest) proof-list-nil-v1)))) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-81219e183feb0f5a9f638c8fb2675bb666edf6cf3311a2716756caf53071ba56
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-assertion-build-environment-nil-v1 proof-list-nil-v1 proof-list-nil-v1 (proof-apply-v1 ProofAssertionBuildEnvironmentV1 (proof-list-cons-v1 (proof-apply-v1 ProofAssertionListNilV1 proof-list-nil-v1) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionListNilV1 proof-list-nil-v1) (proof-list-cons-v1 (proof-apply-v1 ProofEnvironmentNilV1 proof-list-nil-v1) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-131904742a092f64441abaac6dfcb83de4793d7e19053e3f89dc647500fba805
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-assertion-check-disjoints-cons-v1 (proof-list-cons-v1 (proof-formal-v1 Left proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Right proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 DisjointsRest proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Environment proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 LeftImage proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 RightImage proof-zero-v1) proof-list-nil-v1)))))) (proof-list-cons-v1 (proof-apply-v1 ProofTokenDifferentV1 (proof-list-cons-v1 (proof-fvar-v1 Left) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofEnvironmentLookupV1 (proof-list-cons-v1 (proof-fvar-v1 Environment) (proof-list-cons-v1 (proof-fvar-v1 Left) (proof-list-cons-v1 (proof-fvar-v1 LeftImage) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-apply-v1 ProofEnvironmentLookupV1 (proof-list-cons-v1 (proof-fvar-v1 Environment) (proof-list-cons-v1 (proof-fvar-v1 Right) (proof-list-cons-v1 (proof-fvar-v1 RightImage) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceSupportApartV1 (proof-list-cons-v1 (proof-fvar-v1 LeftImage) (proof-list-cons-v1 (proof-fvar-v1 RightImage) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionCheckDisjointsV1 (proof-list-cons-v1 (proof-fvar-v1 DisjointsRest) (proof-list-cons-v1 (proof-fvar-v1 Environment) proof-list-nil-v1))) proof-list-nil-v1))))) (proof-apply-v1 ProofAssertionCheckDisjointsV1 (proof-list-cons-v1 (proof-apply-v1 ProofAssertionListConsV1 (proof-list-cons-v1 (proof-apply-v1 ProofAssertionDisjointV1 (proof-list-cons-v1 (proof-fvar-v1 Left) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1))) (proof-list-cons-v1 (proof-fvar-v1 DisjointsRest) proof-list-nil-v1))) (proof-list-cons-v1 (proof-fvar-v1 Environment) proof-list-nil-v1))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-4d592f414e29baabcf2d02118995937d7e449341fe7e033ddfbcc7e711595fee
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-assertion-check-disjoints-nil-v1 (proof-list-cons-v1 (proof-formal-v1 Environment proof-zero-v1) proof-list-nil-v1) proof-list-nil-v1 (proof-apply-v1 ProofAssertionCheckDisjointsV1 (proof-list-cons-v1 (proof-apply-v1 ProofAssertionListNilV1 proof-list-nil-v1) (proof-list-cons-v1 (proof-fvar-v1 Environment) proof-list-nil-v1))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-79d1cca6fb6eb3a2afdf2cad04a4bc5ea5162da21cff15f7a3febaa1b7dc114d
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-assertion-check-essentials-cons-v1 (proof-list-cons-v1 (proof-formal-v1 Template proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Actual proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 EssentialsRest proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 ActualsRest proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Environment proof-zero-v1) proof-list-nil-v1))))) (proof-list-cons-v1 (proof-apply-v1 MetamathProvableV1 (proof-list-cons-v1 (proof-fvar-v1 Actual) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceInstantiateV1 (proof-list-cons-v1 (proof-fvar-v1 Template) (proof-list-cons-v1 (proof-fvar-v1 Environment) (proof-list-cons-v1 (proof-fvar-v1 Actual) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionCheckEssentialsV1 (proof-list-cons-v1 (proof-fvar-v1 EssentialsRest) (proof-list-cons-v1 (proof-fvar-v1 ActualsRest) (proof-list-cons-v1 (proof-fvar-v1 Environment) proof-list-nil-v1)))) proof-list-nil-v1))) (proof-apply-v1 ProofAssertionCheckEssentialsV1 (proof-list-cons-v1 (proof-apply-v1 ProofAssertionListConsV1 (proof-list-cons-v1 (proof-apply-v1 ProofAssertionEssentialV1 (proof-list-cons-v1 (proof-fvar-v1 Template) proof-list-nil-v1)) (proof-list-cons-v1 (proof-fvar-v1 EssentialsRest) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionListConsV1 (proof-list-cons-v1 (proof-fvar-v1 Actual) (proof-list-cons-v1 (proof-fvar-v1 ActualsRest) proof-list-nil-v1))) (proof-list-cons-v1 (proof-fvar-v1 Environment) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-0d0cc100d9c38ef20469bf81e4884f1e08258fab0ded039bd41f558778283b91
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-assertion-check-essentials-nil-v1 (proof-list-cons-v1 (proof-formal-v1 Environment proof-zero-v1) proof-list-nil-v1) proof-list-nil-v1 (proof-apply-v1 ProofAssertionCheckEssentialsV1 (proof-list-cons-v1 (proof-apply-v1 ProofAssertionListNilV1 proof-list-nil-v1) (proof-list-cons-v1 (proof-apply-v1 ProofAssertionListNilV1 proof-list-nil-v1) (proof-list-cons-v1 (proof-fvar-v1 Environment) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-18deddd08ebf1dbad10884c5bd57c942be8627b5766ad375d6e50c88a0e86ced
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-assertion-use-premise-v1 (proof-list-cons-v1 (proof-formal-v1 Formula proof-zero-v1) proof-list-nil-v1) (proof-list-cons-v1 (proof-apply-v1 MetamathProvableV1 (proof-list-cons-v1 (proof-fvar-v1 Formula) proof-list-nil-v1)) proof-list-nil-v1) (proof-apply-v1 MetamathProvableV1 (proof-list-cons-v1 (proof-fvar-v1 Formula) proof-list-nil-v1)) proof-list-nil-v1)))
      (body))
    (rule answer-fact-5af90db32ea7169ebe71c80daf3e40327d6718daffc4b8da24c16156dc7857f1
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-environment-lookup-head-v1 (proof-list-cons-v1 (proof-formal-v1 Key proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Value proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Tail proof-zero-v1) proof-list-nil-v1))) proof-list-nil-v1 (proof-apply-v1 ProofEnvironmentLookupV1 (proof-list-cons-v1 (proof-apply-v1 ProofEnvironmentConsV1 (proof-list-cons-v1 (proof-fvar-v1 Key) (proof-list-cons-v1 (proof-fvar-v1 Value) (proof-list-cons-v1 (proof-fvar-v1 Tail) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-fvar-v1 Key) (proof-list-cons-v1 (proof-fvar-v1 Value) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-f102b35fac1a1877e00567b1c27d9a545079ecb069321db4a39997660d76c606
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-environment-lookup-tail-v1 (proof-list-cons-v1 (proof-formal-v1 HeadKey proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 HeadValue proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Tail proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Key proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Value proof-zero-v1) proof-list-nil-v1))))) (proof-list-cons-v1 (proof-apply-v1 ProofTokenDifferentV1 (proof-list-cons-v1 (proof-fvar-v1 HeadKey) (proof-list-cons-v1 (proof-fvar-v1 Key) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofEnvironmentLookupV1 (proof-list-cons-v1 (proof-fvar-v1 Tail) (proof-list-cons-v1 (proof-fvar-v1 Key) (proof-list-cons-v1 (proof-fvar-v1 Value) proof-list-nil-v1)))) proof-list-nil-v1)) (proof-apply-v1 ProofEnvironmentLookupV1 (proof-list-cons-v1 (proof-apply-v1 ProofEnvironmentConsV1 (proof-list-cons-v1 (proof-fvar-v1 HeadKey) (proof-list-cons-v1 (proof-fvar-v1 HeadValue) (proof-list-cons-v1 (proof-fvar-v1 Tail) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-fvar-v1 Key) (proof-list-cons-v1 (proof-fvar-v1 Value) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-af1bfcb02437a988560f7898eec2f2166901f31afbb543ea5c1ff25d42026324
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-sequence-append-cons-v1 (proof-list-cons-v1 (proof-formal-v1 Head proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Tail proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Right proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 OutTail proof-zero-v1) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceAppendV1 (proof-list-cons-v1 (proof-fvar-v1 Tail) (proof-list-cons-v1 (proof-fvar-v1 Right) (proof-list-cons-v1 (proof-fvar-v1 OutTail) proof-list-nil-v1)))) proof-list-nil-v1) (proof-apply-v1 ProofSequenceAppendV1 (proof-list-cons-v1 (proof-apply-v1 ProofSequenceConsV1 (proof-list-cons-v1 (proof-fvar-v1 Head) (proof-list-cons-v1 (proof-fvar-v1 Tail) proof-list-nil-v1))) (proof-list-cons-v1 (proof-fvar-v1 Right) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceConsV1 (proof-list-cons-v1 (proof-fvar-v1 Head) (proof-list-cons-v1 (proof-fvar-v1 OutTail) proof-list-nil-v1))) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-0b06578fe09f7df566cdd4876cc0503b9070ac53cc3e950b8e7fabfe9f273f06
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-sequence-append-nil-v1 (proof-list-cons-v1 (proof-formal-v1 Right proof-zero-v1) proof-list-nil-v1) proof-list-nil-v1 (proof-apply-v1 ProofSequenceAppendV1 (proof-list-cons-v1 (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1) (proof-list-cons-v1 (proof-fvar-v1 Right) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-cd09504b87cd185764ea88823271d52835643c429b86d30c5bee9d4b20e45cf4
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-sequence-instantiate-literal-v1 (proof-list-cons-v1 (proof-formal-v1 Head proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Tail proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Environment proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 OutTail proof-zero-v1) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-apply-v1 ProofTokenLiteralV1 (proof-list-cons-v1 (proof-fvar-v1 Head) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceInstantiateV1 (proof-list-cons-v1 (proof-fvar-v1 Tail) (proof-list-cons-v1 (proof-fvar-v1 Environment) (proof-list-cons-v1 (proof-fvar-v1 OutTail) proof-list-nil-v1)))) proof-list-nil-v1)) (proof-apply-v1 ProofSequenceInstantiateV1 (proof-list-cons-v1 (proof-apply-v1 ProofSequenceConsV1 (proof-list-cons-v1 (proof-fvar-v1 Head) (proof-list-cons-v1 (proof-fvar-v1 Tail) proof-list-nil-v1))) (proof-list-cons-v1 (proof-fvar-v1 Environment) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceConsV1 (proof-list-cons-v1 (proof-fvar-v1 Head) (proof-list-cons-v1 (proof-fvar-v1 OutTail) proof-list-nil-v1))) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-581e967189c5c66be4a7ba692b3740b1cc43d3407d4e7922ff1bb8ab8cf2c00b
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-sequence-instantiate-nil-v1 (proof-list-cons-v1 (proof-formal-v1 Environment proof-zero-v1) proof-list-nil-v1) proof-list-nil-v1 (proof-apply-v1 ProofSequenceInstantiateV1 (proof-list-cons-v1 (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1) (proof-list-cons-v1 (proof-fvar-v1 Environment) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-a94b630ebeec006fda1dc6cf14611d722ff315d213e19204e40ae3d66ee74b81
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-sequence-instantiate-variable-v1 (proof-list-cons-v1 (proof-formal-v1 Hole proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Tail proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Environment proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Image proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 OutTail proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Out proof-zero-v1) proof-list-nil-v1)))))) (proof-list-cons-v1 (proof-apply-v1 ProofTokenVariableV1 (proof-list-cons-v1 (proof-fvar-v1 Hole) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 ProofEnvironmentLookupV1 (proof-list-cons-v1 (proof-fvar-v1 Environment) (proof-list-cons-v1 (proof-fvar-v1 Hole) (proof-list-cons-v1 (proof-fvar-v1 Image) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceInstantiateV1 (proof-list-cons-v1 (proof-fvar-v1 Tail) (proof-list-cons-v1 (proof-fvar-v1 Environment) (proof-list-cons-v1 (proof-fvar-v1 OutTail) proof-list-nil-v1)))) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceAppendV1 (proof-list-cons-v1 (proof-fvar-v1 Image) (proof-list-cons-v1 (proof-fvar-v1 OutTail) (proof-list-cons-v1 (proof-fvar-v1 Out) proof-list-nil-v1)))) proof-list-nil-v1)))) (proof-apply-v1 ProofSequenceInstantiateV1 (proof-list-cons-v1 (proof-apply-v1 ProofSequenceConsV1 (proof-list-cons-v1 (proof-fvar-v1 Hole) (proof-list-cons-v1 (proof-fvar-v1 Tail) proof-list-nil-v1))) (proof-list-cons-v1 (proof-fvar-v1 Environment) (proof-list-cons-v1 (proof-fvar-v1 Out) proof-list-nil-v1)))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-1a84bdcabedf17551103cb7a67cdf2f65743d6a23d5032259e102ecd34e51f78
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-sequence-support-apart-cons-v1 (proof-list-cons-v1 (proof-formal-v1 Head proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Tail proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Right proof-zero-v1) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofTokenAgainstSequenceV1 (proof-list-cons-v1 (proof-fvar-v1 Head) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceSupportApartV1 (proof-list-cons-v1 (proof-fvar-v1 Tail) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1))) proof-list-nil-v1)) (proof-apply-v1 ProofSequenceSupportApartV1 (proof-list-cons-v1 (proof-apply-v1 ProofSequenceConsV1 (proof-list-cons-v1 (proof-fvar-v1 Head) (proof-list-cons-v1 (proof-fvar-v1 Tail) proof-list-nil-v1))) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-399466c64ff92d4a7f874a78e9ec54ca45100c3d8905218a8beeb6788cb62185
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-sequence-support-apart-nil-v1 (proof-list-cons-v1 (proof-formal-v1 Right proof-zero-v1) proof-list-nil-v1) proof-list-nil-v1 (proof-apply-v1 ProofSequenceSupportApartV1 (proof-list-cons-v1 (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-02f876b9b33bf7e096554d6f6600a66052c91bf82a2f79b17cc49d52f99103e7
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-token-against-sequence-cons-v1 (proof-list-cons-v1 (proof-formal-v1 Token proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Head proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Tail proof-zero-v1) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofTokenPairAllowedV1 (proof-list-cons-v1 (proof-fvar-v1 Token) (proof-list-cons-v1 (proof-fvar-v1 Head) proof-list-nil-v1))) (proof-list-cons-v1 (proof-apply-v1 ProofTokenAgainstSequenceV1 (proof-list-cons-v1 (proof-fvar-v1 Token) (proof-list-cons-v1 (proof-fvar-v1 Tail) proof-list-nil-v1))) proof-list-nil-v1)) (proof-apply-v1 ProofTokenAgainstSequenceV1 (proof-list-cons-v1 (proof-fvar-v1 Token) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceConsV1 (proof-list-cons-v1 (proof-fvar-v1 Head) (proof-list-cons-v1 (proof-fvar-v1 Tail) proof-list-nil-v1))) proof-list-nil-v1))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-d3247e2e7f09712df1aba65439bd5065231012513364d9819206b742c42c3743
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-token-against-sequence-nil-v1 (proof-list-cons-v1 (proof-formal-v1 Token proof-zero-v1) proof-list-nil-v1) proof-list-nil-v1 (proof-apply-v1 ProofTokenAgainstSequenceV1 (proof-list-cons-v1 (proof-fvar-v1 Token) (proof-list-cons-v1 (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1) proof-list-nil-v1))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-619f00971c4c0b361702b6ae07ddad00ff48f470d7c865562902a859135a20c5
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-token-pair-apart-v1 (proof-list-cons-v1 (proof-formal-v1 Left proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Right proof-zero-v1) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 ProofTokenVariableV1 (proof-list-cons-v1 (proof-fvar-v1 Left) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 ProofTokenVariableV1 (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 ProofTokenApartV1 (proof-list-cons-v1 (proof-fvar-v1 Left) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1))) proof-list-nil-v1))) (proof-apply-v1 ProofTokenPairAllowedV1 (proof-list-cons-v1 (proof-fvar-v1 Left) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-7e47b4f76caf712e33172f931ac438dcf3097ebc53fd90875ea5b1c09d7d1263
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-token-pair-left-literal-v1 (proof-list-cons-v1 (proof-formal-v1 Left proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Right proof-zero-v1) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 ProofTokenLiteralV1 (proof-list-cons-v1 (proof-fvar-v1 Left) proof-list-nil-v1)) proof-list-nil-v1) (proof-apply-v1 ProofTokenPairAllowedV1 (proof-list-cons-v1 (proof-fvar-v1 Left) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-ed8eba10176946da4917e0ac6317132457beee3886915933742cdc5204fe5105
      (head (proof-artifact-v1 (proof-rule-v1 MetamathProofV1 proof-token-pair-right-literal-v1 (proof-list-cons-v1 (proof-formal-v1 Left proof-zero-v1) (proof-list-cons-v1 (proof-formal-v1 Right proof-zero-v1) proof-list-nil-v1)) (proof-list-cons-v1 (proof-apply-v1 ProofTokenLiteralV1 (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1)) proof-list-nil-v1) (proof-apply-v1 ProofTokenPairAllowedV1 (proof-list-cons-v1 (proof-fvar-v1 Left) (proof-list-cons-v1 (proof-fvar-v1 Right) proof-list-nil-v1))) proof-list-nil-v1)))
      (body))
    (rule answer-fact-7c044a388bb16d1113b43d9449707cee776f1b487ac2757859190271157ad1fd
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-identity-v1 MetamathProofV1 MetamathLanguageV1)))
      (body))
    (rule answer-fact-20ff290cdb2be46534d37b2b64d75180b27c763c7d1297e35c46d82666b9fbd4
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-selector-v1 MetamathProofV1 proof-relational-hypothesis-binding-v1 mm-label-floating)))
      (body))
    (rule answer-fact-7b06ded55f43dee657f8c2d35932f6d37f27554894a54d13a537509aede10e24
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-selector-v1 MetamathProofV1 proof-relational-hypothesis-essential-v1 mm-label-essential)))
      (body))
    (rule answer-fact-3078d9125ecc2b3b09f342f6d0a62f8afe29f0a5a9d6936cb027ac46c7defa24
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-selector-v1 MetamathProofV1 proof-relational-label-assertion-v1 mm-label-axiom)))
      (body))
    (rule answer-fact-21143709d19bd5e6ecbae121a7cf1253a6321b3c3cb90504a9fbb2edda75b55f
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-selector-v1 MetamathProofV1 proof-relational-label-assertion-v1 mm-label-theorem)))
      (body))
    (rule answer-fact-ef13e59e3f2d9feeed9c883c9b1050a3dffe904b9402d582ff7a5f3c1c7d97f5
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-selector-v1 MetamathProofV1 proof-relational-token-literal-v1 mm-symbol-constant)))
      (body))
    (rule answer-fact-c387e5467be1cc1b5c555dd7fc955b1090015744e00acb29cfc41ab21cfdaa7a
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-selector-v1 MetamathProofV1 proof-relational-token-variable-v1 mm-symbol-variable)))
      (body))
    (rule answer-fact-3bd1f2db7900ecae40fbbd5d6c6940e208f869edce8f34b23d6aef41f9436dfe
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-table-v1 MetamathProofV1 proof-relational-active-apartness-v1 mm-state-disjoint-variable 2 2)))
      (body))
    (rule answer-fact-168be38e5de8dd1acec4690b079a15aa51c2efea72fca609f3c9f0ffc25277af
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-table-v1 MetamathProofV1 proof-relational-assertion-apartness-v1 mm-state-assertion-ordered-disjoint-variable 4 2)))
      (body))
    (rule answer-fact-34598dbbad967a168a407b2156723cc766f3a372c7c59671197a9911190d3f95
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-table-v1 MetamathProofV1 proof-relational-floating-binding-v1 mm-state-floating-variable 2 1)))
      (body))
    (rule answer-fact-6df0d2718a4eac1a4d88c1d561e79fa3e520ec91fb6e58a58d8b3f6fad8db694
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-table-v1 MetamathProofV1 proof-relational-formula-v1 mm-state-formula 2 1)))
      (body))
    (rule answer-fact-7fba48f3268c13cfe2c72ea83b2ee77b4d069d3cdd0826a4a57970be209adb59
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-table-v1 MetamathProofV1 proof-relational-label-class-v1 mm-state-label-kind 2 1)))
      (body))
    (rule answer-fact-108c4e428912e9d41493e8d09d992cc60e49b3d9e1be4c7272682c6c2000e60d
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-table-v1 MetamathProofV1 proof-relational-mandatory-variable-v1 mm-state-assertion-mandatory-variable 2 2)))
      (body))
    (rule answer-fact-23ed589334972789641df87f26f7ac334640744e9dff7b6f79b4a9aef7fd6ea0
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-table-v1 MetamathProofV1 proof-relational-ordered-context-v1 mm-state-assertion-ordered-hypothesis 3 2)))
      (body))
    (rule answer-fact-7eb25bbe6b0ef215a469e9484c71618ea70ee0c115d240b470566dd74a416bf0
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-table-v1 MetamathProofV1 proof-relational-ordered-frame-v1 mm-state-assertion-ordered-frame-hypothesis 3 2)))
      (body))
    (rule answer-fact-736aeb34c0e460d81193bb0673710f7dd98bccb828f2d613d89b098490b36be3
      (head (proof-relational-projection-artifact-v1 (proof-relational-projection-table-v1 MetamathProofV1 proof-relational-symbol-class-v1 mm-state-symbol-kind 2 1)))
      (body))
    (rule answer-fact-d2d66e61318c3c2b479b92512d52751207caf27405cab0eccd046115b306e524
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-binary-table-enumeration-v1 MetamathProofV1 source-proof-relational-active-apartness-row-v1 source-proof-relational-active-apartness-count-v1 mm-state-disjoint-variable 0 1 ProofTraceNatZeroV1 ProofTraceNatSuccV1)))
      (body))
    (rule answer-fact-1a42a776bcc0e7001b8b188e455743cec5518688ffcb3ee8e25b4b0574be22c5
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-compressed-input-v1 MetamathProofV1 ProofRelationalRequestV1 ProofRelationalLabelV1 ProofTraceLabelConsV1 ProofTraceLabelNilV1 ProofTraceCodeConsV1 ProofTraceCodeNilV1 mm-state-assertion-ordered-frame-hypothesis 0 1 2)))
      (body))
    (rule answer-fact-3a2af9ec0e5622462795f96f007da1f136e9a14b7e7077a415e85ca916f9ac94
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-control-literal-v1 MetamathProofV1 mm-state-label-kind 1 mm-label-axiom)))
      (body))
    (rule answer-fact-52291eb4f7529a3992ccb1fc21bed65318ba322ff99eb8d380a10f503b8c9e6a
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-control-literal-v1 MetamathProofV1 mm-state-label-kind 1 mm-label-essential)))
      (body))
    (rule answer-fact-37bead139bd5530ecd78318666c7a87e9a054d814408eb1585758760f667e17e
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-control-literal-v1 MetamathProofV1 mm-state-label-kind 1 mm-label-floating)))
      (body))
    (rule answer-fact-3b23c0810054954344dd85a454c15f0b75e81c28a28afc5607417f5111d6ace7
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-control-literal-v1 MetamathProofV1 mm-state-label-kind 1 mm-label-theorem)))
      (body))
    (rule answer-fact-70cf502b0ff8a2bc91cac92fadaf0ed2d07c20bafffa9776812bcdcc3d03fc0a
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-control-literal-v1 MetamathProofV1 mm-state-symbol-kind 1 mm-symbol-constant)))
      (body))
    (rule answer-fact-7315496110ec06186fcdcdc2d2c677da61ada615b8178e26b1672a1d2493c7a4
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-control-literal-v1 MetamathProofV1 mm-state-symbol-kind 1 mm-symbol-variable)))
      (body))
    (rule answer-fact-3a4c9b0fb389727f23f99805fbcc2f39525f0eb265c567774033a4d4ac0edd67
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-distinct-pairs-v1 MetamathProofV1 source-proof-relational-value-different-v1 mm-state-label-kind 0)))
      (body))
    (rule answer-fact-ecb0ba663ff6291bc759200e2fb17ec9fe9a8b2b15100c0d14ab4f97c9c19fa2
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-distinct-pairs-v1 MetamathProofV1 source-proof-relational-value-different-v1 mm-state-symbol-kind 0)))
      (body))
    (rule answer-fact-8f0fe653b852a0718c7440152723fd38b2c1d76cb67bd0f4bf0194dc901096ef
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-identity-v1 MetamathProofV1 MetamathLanguageV1)))
      (body))
    (rule answer-fact-4fa06166511da532deb2f45031da08df9bf2c86dcc6f952e00a21026ec53194b
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-input-byte-v1 MetamathProofV1 proof-runtime-normal-input-v1 source-proof-trace-input-incomplete-normal-label-v1 63)))
      (body))
    (rule answer-fact-1a318a183595bb24d1c91212fc12af6a72f0181d9ade9a4eecf29809c43acd4e
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-normal-input-v1 MetamathProofV1 ProofRelationalRequestV1 ProofRelationalLabelV1 ProofTraceLabelConsV1 ProofTraceLabelNilV1)))
      (body))
    (rule answer-fact-15f0a16fbbfcf7830293c4df2df8b65692f89a116bdffef5777bb88508938da0
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-ordinal-column-v1 MetamathProofV1 mm-state-assertion-ordered-disjoint-variable 1 ProofTraceNatZeroV1 ProofTraceNatSuccV1)))
      (body))
    (rule answer-fact-abc51291fec8f6defc9989ff4a87134155cc8cf0d53364f6a5e09d60148b8878
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-ordinal-column-v1 MetamathProofV1 mm-state-assertion-ordered-frame-hypothesis 1 ProofTraceNatZeroV1 ProofTraceNatSuccV1)))
      (body))
    (rule answer-fact-695e0bada61d5bcf95de2706e2b3b588f9b82520de0f9b68c44a7f6ab625135f
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-ordinal-column-v1 MetamathProofV1 mm-state-assertion-ordered-hypothesis 1 ProofTraceNatZeroV1 ProofTraceNatSuccV1)))
      (body))
    (rule answer-fact-b7860eb458bc262ce460422ff20715671966856fb33a88918157cc9a1954b3c1
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-outcome-query-v1 MetamathProofV1 proof-runtime-compressed-input-v1 0 proof-runtime-result-verified-v1 ProofTraceInputVerifyCompressedV1)))
      (body))
    (rule answer-fact-69dd49918584c4f4ef227f6596a31afe1233bbdedbcf462360e9ba88eafda1ce
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-outcome-query-v1 MetamathProofV1 proof-runtime-compressed-input-v1 1 proof-runtime-result-incomplete-v1 ProofTraceInputAcceptIncompleteCompressedV1)))
      (body))
    (rule answer-fact-732ae5cec8a23211b03244fe3c592769ebbbdb936f5582c5e62147c58be16bc3
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-outcome-query-v1 MetamathProofV1 proof-runtime-normal-input-v1 0 proof-runtime-result-verified-v1 ProofTraceInputVerifyNormalV1)))
      (body))
    (rule answer-fact-d90a0662a38379fc227054bdeea5a697977514c700bf5bd85c84983acaa3855a
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-outcome-query-v1 MetamathProofV1 proof-runtime-normal-input-v1 1 proof-runtime-result-incomplete-v1 ProofTraceInputAcceptIncompleteNormalV1)))
      (body))
    (rule answer-fact-e0d6bada54a3e823131569f71f6bd45c56292dd17b58f62fa96bb99a952bb0fb
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-prefix-count-v1 MetamathProofV1 source-proof-trace-input-apartness-count-v1 ProofRelationalLabelV1 mm-state-assertion-ordered-disjoint-variable ProofTraceNatZeroV1 ProofTraceNatSuccV1 proof-runtime-proof-labels-v1)))
      (body))
    (rule answer-fact-e080ee27bbfddee922859d5cbced08e62bb0f56685d526beaad99e9c42af9f2c
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-prefix-count-v1 MetamathProofV1 source-proof-trace-input-context-count-v1 ProofRelationalRequestV1 mm-state-assertion-ordered-hypothesis ProofTraceNatZeroV1 ProofTraceNatSuccV1 proof-runtime-request-label-v1)))
      (body))
    (rule answer-fact-f3d80b91cb9ba64d0aeb72bb3749960c0558e727cbda50044afa0104e4a3be6c
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-prefix-count-v1 MetamathProofV1 source-proof-trace-input-frame-count-v1 ProofRelationalLabelV1 mm-state-assertion-ordered-frame-hypothesis ProofTraceNatZeroV1 ProofTraceNatSuccV1 proof-runtime-proof-labels-v1)))
      (body))
    (rule answer-fact-19ea6c660787cb37c75171c348f24680561b1f71a5195664d3b3f2a62f5b3ba8
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-request-v1 MetamathProofV1 source-proof-relational-request-v1 ProofRelationalRequestV1)))
      (body))
    (rule answer-fact-8b2b66f6ee6f7c336abec26e00c326f3c29f1c98c46e5013946216d6a156b1d0
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-state-table-v1 MetamathProofV1 source-proof-relational-row2-v1 mm-state-assertion-mandatory-variable 2 2)))
      (body))
    (rule answer-fact-e1269aeea62506333561d1cd4d088d4fb4715dbf54ae5fa1a8fad4788cfad9a0
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-state-table-v1 MetamathProofV1 source-proof-relational-row2-v1 mm-state-disjoint-variable 2 2)))
      (body))
    (rule answer-fact-e01ec1eda0955a8fe4b4fcf8fd99c9159b95ada0c517545729e12e5897644874
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-state-table-v1 MetamathProofV1 source-proof-relational-row2-v1 mm-state-floating-variable 2 1)))
      (body))
    (rule answer-fact-3b06f70837dfd57328b5387730b0c563717a76b1f54182120c6cf3d676e96a8f
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-state-table-v1 MetamathProofV1 source-proof-relational-row2-v1 mm-state-formula 2 1)))
      (body))
    (rule answer-fact-9881088cd40788d1e665ca824cec8c3418747aba4c58ff15ad74b38cb2fb9fe7
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-state-table-v1 MetamathProofV1 source-proof-relational-row2-v1 mm-state-label-kind 2 1)))
      (body))
    (rule answer-fact-55a82460f6946c852b2e5b166851c77fd0c1a68ba9d80eba26a47bc180a2faff
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-state-table-v1 MetamathProofV1 source-proof-relational-row2-v1 mm-state-symbol-kind 2 1)))
      (body))
    (rule answer-fact-cef781a12e82b00345aa14e8738ed224bfc72d1497b55db68f64bb190e51d35b
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-state-table-v1 MetamathProofV1 source-proof-relational-row3-v1 mm-state-assertion-ordered-frame-hypothesis 3 2)))
      (body))
    (rule answer-fact-8335ff5c8be6f855823e41973bad8e7d0328414c3cbb8a6e5c87ab7b8d7826ea
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-state-table-v1 MetamathProofV1 source-proof-relational-row3-v1 mm-state-assertion-ordered-hypothesis 3 2)))
      (body))
    (rule answer-fact-98959ee88e7516a8810e100c58a40f115dff6f6e3f3d34c5524d20a8134b1580
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-state-table-v1 MetamathProofV1 source-proof-relational-row4-v1 mm-state-assertion-ordered-disjoint-variable 4 2)))
      (body))
    (rule answer-fact-d5a3f10ea5c41bc420eb68f3d8be094c4b0850b9dedc2db3580f09ef6cef23b5
      (head (proof-relational-runtime-artifact-v1 (proof-relational-runtime-value-list-v1 MetamathProofV1 source-proof-relational-value-list-v1 ProofRelationalValueListConsV1 ProofRelationalValueListNilV1)))
      (body))
    (rule answer-fact-8381dc49762482db2aeee2ae33ec6716ec7d50bc3d0e769cbe080c54d5f414c6
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-constructor-role-v1 MetamathProofV1 MetamathLanguageV1 assertion-v1 ProofAssertionV1)))
      (body))
    (rule answer-fact-53918424f124c33e20cb0fd0da5ccd17b9d739bad384b4aa75cb7393119d05df
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-constructor-role-v1 MetamathProofV1 MetamathLanguageV1 disjoint-v1 ProofAssertionDisjointV1)))
      (body))
    (rule answer-fact-9c1c427f1447fbf02bcf551e28e2daec52d7b130ca05f1245c0f841b425997ed
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-constructor-role-v1 MetamathProofV1 MetamathLanguageV1 essential-v1 ProofAssertionEssentialV1)))
      (body))
    (rule answer-fact-21d8cfc8362f14789994b4d0531b27afccdf2a57d09a6af70f643c9271b2d548
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-constructor-role-v1 MetamathProofV1 MetamathLanguageV1 list-cons-v1 ProofAssertionListConsV1)))
      (body))
    (rule answer-fact-19ed296c8d44ee2149d0f1a259726273bfe6bc0fc0df516382d92b8852d11d5b
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-constructor-role-v1 MetamathProofV1 MetamathLanguageV1 list-nil-v1 ProofAssertionListNilV1)))
      (body))
    (rule answer-fact-ff8f90f2a91d99831613a055c1fef818f3a18b5a791726c2824de439e85fb430
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-constructor-role-v1 MetamathProofV1 MetamathLanguageV1 variable-v1 ProofAssertionVariableV1)))
      (body))
    (rule answer-fact-8595055c288cc0066e424611ba80fc3c24162a9996c7f2cc1162614395bfd16c
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-judgment-role-v1 MetamathProofV1 build-environment-v1 ProofAssertionBuildEnvironmentV1)))
      (body))
    (rule answer-fact-7e51e3fa4ee7b669e8b087966942fe5a2b4d33b08863545157927193b38efca5
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-judgment-role-v1 MetamathProofV1 check-disjoints-v1 ProofAssertionCheckDisjointsV1)))
      (body))
    (rule answer-fact-6cefa0935d87dfbefb26dc69819098ceee3c39470c0df8d483fc52568333175d
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-judgment-role-v1 MetamathProofV1 check-essentials-v1 ProofAssertionCheckEssentialsV1)))
      (body))
    (rule answer-fact-1013fb00704f25aa051d1982e33692e427f20b5df3311844971bec1f4a21d024
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-judgment-role-v1 MetamathProofV1 declared-v1 ProofAssertionDeclaredV1)))
      (body))
    (rule answer-fact-1a9ac812de7447f21af5065c8b1ff2dc5b63f9292fd0312780a30a6254e546a5
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-judgment-role-v1 MetamathProofV1 provable-v1 MetamathProvableV1)))
      (body))
    (rule answer-fact-7dfedfb933d44fbd82e9342f71c6cbb227e154c2c9519b1885cd80f8b3a81a9a
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-rule-role-v1 MetamathProofV1 apply-v1 proof-assertion-apply-v1)))
      (body))
    (rule answer-fact-6ea949d41bd7ad7ce07ee675694b2f98a1c3ca839b4b5acd202faf2df7322518
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-rule-role-v1 MetamathProofV1 build-environment-cons-v1 proof-assertion-build-environment-cons-v1)))
      (body))
    (rule answer-fact-7911889e63d389e9c5783bf1b235a9b158d87bab6998643b2bec0b4eecd7f52e
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-rule-role-v1 MetamathProofV1 build-environment-nil-v1 proof-assertion-build-environment-nil-v1)))
      (body))
    (rule answer-fact-d4c1a32196078783f87ff1e8690aabfa19ee3172b7120957a35f290ee8ec3de0
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-rule-role-v1 MetamathProofV1 check-disjoints-cons-v1 proof-assertion-check-disjoints-cons-v1)))
      (body))
    (rule answer-fact-3dc7e471c728477b9662dd03c5cfa83129e5228fb010bffe5628ef008f8165ba
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-rule-role-v1 MetamathProofV1 check-disjoints-nil-v1 proof-assertion-check-disjoints-nil-v1)))
      (body))
    (rule answer-fact-0bad1699b1721878277dffc9a500ee1ffcc3c47c942d07570232161d5e4be78c
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-rule-role-v1 MetamathProofV1 check-essentials-cons-v1 proof-assertion-check-essentials-cons-v1)))
      (body))
    (rule answer-fact-73039cdb4de043792454f64cca1bb54631c4ca912e841c2f6d3d311a0379a239
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-rule-role-v1 MetamathProofV1 check-essentials-nil-v1 proof-assertion-check-essentials-nil-v1)))
      (body))
    (rule answer-fact-76d261d295ed4dc13a050c00ddcc217ded82231f0279e2247752be97ef2e7bb2
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-assertion-rule-role-v1 MetamathProofV1 use-premise-v1 proof-assertion-use-premise-v1)))
      (body))
    (rule answer-fact-0ef1c83d6d3775e2e5a9a090dea64d18e3c5e2bfe4066c778825be6fc3decd6f
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-constructor-role-v1 MetamathProofV1 MetamathLanguageV1 environment-cons-v1 ProofEnvironmentConsV1)))
      (body))
    (rule answer-fact-45a6c6faa0377b5add1d4fd994c9d139f1ab4644ef8f2bda6fbd42f9e9aa44e8
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-constructor-role-v1 MetamathProofV1 MetamathLanguageV1 environment-nil-v1 ProofEnvironmentNilV1)))
      (body))
    (rule answer-fact-a74f51241aadf14d6f1405d4b7000d250eec40c28aafb4763d83878b2d3ef743
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-constructor-role-v1 MetamathProofV1 MetamathLanguageV1 sequence-cons-v1 ProofSequenceConsV1)))
      (body))
    (rule answer-fact-ba1528406c5806ab84611dc42b39174d9381bef17c2045699bf128f2c45fad83
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-constructor-role-v1 MetamathProofV1 MetamathLanguageV1 sequence-nil-v1 ProofSequenceNilV1)))
      (body))
    (rule answer-fact-8cd82fd7bf086e8f1fbd23d4dd0919a85039dd303daac241a587a3aca34a19eb
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-judgment-role-v1 MetamathProofV1 apart-v1 ProofTokenApartV1)))
      (body))
    (rule answer-fact-925e2830f28bc4a7e3f6812bc0a6e31a1a82c1898b270869bc64ccb99c4f0853
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-judgment-role-v1 MetamathProofV1 append-v1 ProofSequenceAppendV1)))
      (body))
    (rule answer-fact-c3fb2837fc1b1c816baf15d901640cd6e5503a99560962dd2d0cb481c5ab0c88
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-judgment-role-v1 MetamathProofV1 different-v1 ProofTokenDifferentV1)))
      (body))
    (rule answer-fact-5de2ebf09b36695aa9c477958e3657b7f9cbdc3e4cbcf8ca16f2effa58979086
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-judgment-role-v1 MetamathProofV1 instantiate-v1 ProofSequenceInstantiateV1)))
      (body))
    (rule answer-fact-3d2683e6e7dc32def3896298a95b31c16b886e55f203024449b2be23a30f13bf
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-judgment-role-v1 MetamathProofV1 literal-v1 ProofTokenLiteralV1)))
      (body))
    (rule answer-fact-13d8fcdba9e2b7d350093c15cb1b9b9270b7d9c9b3e3a9aee291576ab430d9df
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-judgment-role-v1 MetamathProofV1 lookup-v1 ProofEnvironmentLookupV1)))
      (body))
    (rule answer-fact-48d41519531874a63d791109c5ce66e60c3301b9b4f98315727f15b5553c6417
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-judgment-role-v1 MetamathProofV1 pair-allowed-v1 ProofTokenPairAllowedV1)))
      (body))
    (rule answer-fact-00d0a342d6e4bad6e76588e096d7568ed2c9f0c6281613fad3962191c32e7547
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-judgment-role-v1 MetamathProofV1 support-apart-v1 ProofSequenceSupportApartV1)))
      (body))
    (rule answer-fact-f7bb0e9cd20ea2a3c5c5e6dc6a842da939cda511e0943211ca3f4cdc3ede001c
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-judgment-role-v1 MetamathProofV1 token-against-sequence-v1 ProofTokenAgainstSequenceV1)))
      (body))
    (rule answer-fact-e96e7b0f2e8edbf6601a74b7f3ee6dbfafc9b200df42a66f8c82426922caafd7
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-judgment-role-v1 MetamathProofV1 variable-v1 ProofTokenVariableV1)))
      (body))
    (rule answer-fact-4e5d109ea228b2f46d53bd37fe2fd33f970bbb44f0fe2ff3eaa970aad4cc5952
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 append-cons-v1 proof-sequence-append-cons-v1)))
      (body))
    (rule answer-fact-a26f851cce7e3979c052da9d74bbb158c4d5534b5a42ade9e61ca3bc2d5cdd7f
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 append-nil-v1 proof-sequence-append-nil-v1)))
      (body))
    (rule answer-fact-ae2f5b1d9d188798530dd788c4fee7b9df1e59602f33a25fee5a380bc4895e9d
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 instantiate-literal-v1 proof-sequence-instantiate-literal-v1)))
      (body))
    (rule answer-fact-d4623f6fa7bbc7bebeca519996328604129f821c704618e3c145a6a5007f0fac
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 instantiate-nil-v1 proof-sequence-instantiate-nil-v1)))
      (body))
    (rule answer-fact-0a1c1866269441e6bf356799bdf601c91216361ab04d79240dc11414b3258a36
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 instantiate-variable-v1 proof-sequence-instantiate-variable-v1)))
      (body))
    (rule answer-fact-170871599596411d8c0b5e0e9729a96fb263c59c78fe7cb225877889d1724de9
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 lookup-head-v1 proof-environment-lookup-head-v1)))
      (body))
    (rule answer-fact-3a12fe97a6e703a769576e9cacaa732880638c3abc412f10667f78ee4151f27e
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 lookup-tail-v1 proof-environment-lookup-tail-v1)))
      (body))
    (rule answer-fact-2be79af69981679e6eb12ffac9bd6d6f0130fe3114ec28dc37168185d462b175
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 pair-apart-v1 proof-token-pair-apart-v1)))
      (body))
    (rule answer-fact-f93e0b3a561d0f8e8cc94bb180f41412a4a2b0500ab9bb7e8072df1c2be11535
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 pair-left-literal-v1 proof-token-pair-left-literal-v1)))
      (body))
    (rule answer-fact-a391fdd7296d3df5075a2f14023a571093398d1a0db3fbbf54e005848e83b123
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 pair-right-literal-v1 proof-token-pair-right-literal-v1)))
      (body))
    (rule answer-fact-c6f17c1879ee693fb294df06a08c7b7b79fe67e9d2254729c4527e136c72ae57
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 support-apart-cons-v1 proof-sequence-support-apart-cons-v1)))
      (body))
    (rule answer-fact-eab4f8483386768c95bc7f539b23b140fb296c337255fc693b092ffe65cad47d
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 support-apart-nil-v1 proof-sequence-support-apart-nil-v1)))
      (body))
    (rule answer-fact-a7219f8908a8e9de5899d99dcbe0fdcd845e4bc856a2981a46fdd5169b6ab58c
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 token-against-cons-v1 proof-token-against-sequence-cons-v1)))
      (body))
    (rule answer-fact-575b6d6dde49bec4140a912b81927c34a7e478dd53becb41eaf50aebec533c41
      (head (proof-sequence-evidence-artifact-v1 (proof-sequence-evidence-rule-role-v1 MetamathProofV1 token-against-nil-v1 proof-token-against-sequence-nil-v1)))
      (body))
    (rule answer-fact-50b1811732c281eea8005e4633ba4fcf6d208aae7f1df1e86752de6e7d7965f5
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-execution-v1 MetamathProofV1 mm-stack-proof-machine-v1 (state-byte-literal-v1 63) 65 84 85 89 90 63 20 0 5 1 state-proof-unknown-push-claim-v1 state-proof-save-immediately-after-use-v1 state-proof-header-nonmandatory-only-v1)))
      (body))
    (rule answer-fact-67d08f434e0f16c9c22eb3c84e56aa1ca2628e6ec712cc4ed1cc06fe8185711c
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-identity-v1 MetamathProofV1 MetamathLanguageV1)))
      (body))
    (rule answer-fact-005f6870ed09e85ddde75d2691ea2f9a945a7412f47893b939f33b4d6c62253a
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-selector-v1 MetamathProofV1 hypothesis-essential-v1 mm-label-essential)))
      (body))
    (rule answer-fact-2410cf84f4ffc6753c1177933953f2dd6783ba781c09e3f9f0d6be1951dd96bf
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-selector-v1 MetamathProofV1 hypothesis-floating-v1 mm-label-floating)))
      (body))
    (rule answer-fact-188a52c00536135a11e773ab65d8773f524c4ae2c8dfdb9ae1ddb51a0eb4a41a
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-selector-v1 MetamathProofV1 symbol-literal-v1 mm-symbol-constant)))
      (body))
    (rule answer-fact-fa274ebc952aefbe947a10de57ae1905fa2fb3d4c443dbe7ee619630a3d8fb42
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-selector-v1 MetamathProofV1 symbol-variable-v1 mm-symbol-variable)))
      (body))
    (rule answer-fact-d65c40981252b61947b830f4ebad1e2c863229e50743d59a23461ccf37e5611c
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-policy-v1 MetamathProofV1 active-apartness-v1 optional-empty-v1)))
      (body))
    (rule answer-fact-7880900bbcd0e9857bcdc3646ba3aa48f3db2d7dc81de015dacba4d60352d554
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-policy-v1 MetamathProofV1 assertion-active-hypothesis-v1 required-v1)))
      (body))
    (rule answer-fact-0cc24aa474b6cf400344c96db4f32314b5b596782a69b2fb08057c136314cfac
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-policy-v1 MetamathProofV1 assertion-disjoint-v1 optional-empty-v1)))
      (body))
    (rule answer-fact-7ca015e0633947447d4bc8eaa43bfc396a422347e2414dfc0960b64d323fe341
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-policy-v1 MetamathProofV1 floating-variable-v1 required-v1)))
      (body))
    (rule answer-fact-90fe3cd2706e1beb22b5fea409330550bcca43e2945b9ad9f652929f8a850ad9
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-policy-v1 MetamathProofV1 formula-v1 required-v1)))
      (body))
    (rule answer-fact-4cdc7556d4fadd79c5d4cf5b726cf49a84713c0f20278c7b8b0fb9cb6048f7b9
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-policy-v1 MetamathProofV1 label-kind-v1 required-v1)))
      (body))
    (rule answer-fact-a3f0651fcd33f2efee727b35ce41c35b764e39ff5e1f02837ea664d5a132d5a6
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-policy-v1 MetamathProofV1 mandatory-variable-v1 required-v1)))
      (body))
    (rule answer-fact-213b94f95e25b418ccef5a07271a2a9bb5a3293e68dd74e9b9be1b0146881f5f
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-policy-v1 MetamathProofV1 ordered-hypothesis-v1 required-v1)))
      (body))
    (rule answer-fact-f93e48ec935064f0feabcd6f888d8db01c3658b44b7139ab26a6d675a7eb031d
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-policy-v1 MetamathProofV1 symbol-kind-v1 required-v1)))
      (body))
    (rule answer-fact-36b3eac5bb922ac8bafd149c58a9c514209837dd6df3c29ad433f8e7b79f8a33
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-v1 MetamathProofV1 active-apartness-v1 mm-state-disjoint-variable 2 2)))
      (body))
    (rule answer-fact-b3651b94f988ba26013d66c550ed87e788f82c7681adf88f3898030e64747da3
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-v1 MetamathProofV1 assertion-active-hypothesis-v1 mm-state-assertion-active-hypothesis 2 2)))
      (body))
    (rule answer-fact-2eb3be6f7d76935443283b83cfc3364fd3eb9982a9e6c3bf1c55ab2b24bff8dc
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-v1 MetamathProofV1 assertion-disjoint-v1 mm-state-assertion-disjoint-variable 3 3)))
      (body))
    (rule answer-fact-2f20eb637ff0679e0e5f75689d485e5b9ce941bd3eecef57abefe1d9a46f82eb
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-v1 MetamathProofV1 floating-variable-v1 mm-state-floating-variable 2 1)))
      (body))
    (rule answer-fact-0a9f5cc8f7db28c16b9748a5a8442853270ab97386982fe873235c1773813d67
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-v1 MetamathProofV1 formula-v1 mm-state-formula 2 1)))
      (body))
    (rule answer-fact-29b67b02b8db856910a4f457afb242380fc9d679fd2209a8a16987b2ffeba3ea
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-v1 MetamathProofV1 label-kind-v1 mm-state-label-kind 2 1)))
      (body))
    (rule answer-fact-988a500a1728b5de6988194e140957291580df567ed7246fd55d46ef8bc3f242
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-v1 MetamathProofV1 mandatory-variable-v1 mm-state-assertion-mandatory-variable 2 2)))
      (body))
    (rule answer-fact-3fa3b1a4dbb0ec2539ded9ff5ff209ef53e3a327f1bbc2fb91a6e09abb078e93
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-v1 MetamathProofV1 ordered-hypothesis-v1 mm-state-assertion-ordered-hypothesis 3 2)))
      (body))
    (rule answer-fact-3806f45f3616ca911fe764f478a3730ea05ea2d47651c88e60a4a23c7c377d5f
      (head (proof-sequence-relational-artifact-v1 (proof-sequence-relational-table-v1 MetamathProofV1 symbol-kind-v1 mm-state-symbol-kind 2 1)))
      (body))
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
    (rule answer-fact-bc055ad4cbb61beffb33939d058cafc50418223f827a4e11d34fcc46db101f71
      (head (state-proof-action-v1 mm-stack-proof-machine-v1 mm-label-axiom stack-apply-frame-v1))
      (body))
    (rule answer-fact-071589fde187547ac7b650914cc741a0c14dcf3210454d41ef1d988e4b2b9182
      (head (state-proof-action-v1 mm-stack-proof-machine-v1 mm-label-essential stack-push-declared-v1))
      (body))
    (rule answer-fact-8090a23cbf2055efbf1a5ba394c091ccd92c485e7916929b0877bdbb98ec6060
      (head (state-proof-action-v1 mm-stack-proof-machine-v1 mm-label-floating stack-push-declared-v1))
      (body))
    (rule answer-fact-fceea71e53c74ed8e72ef1452e008a86733d13f4eb8515603ef08fbee6bf19e5
      (head (state-proof-action-v1 mm-stack-proof-machine-v1 mm-label-theorem stack-apply-frame-v1))
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
