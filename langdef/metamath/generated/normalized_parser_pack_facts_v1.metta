; generated ParserPack facts for staged compiler GSLTs
(gslt-presentation-v1 GeneratedParserPackFactsV1-cb13116413e8b0133798af5a900b216e9883585194fa5eb5e8d13cb5438ccd3e
  (signature
    (operator compile-pack-production 2)
    (operator compile-pack-source-occurrence 3)
    (operator cp 1)
    (operator pa-apply 2)
    (operator pa-cons 2)
    (operator pa-const 1)
    (operator pa-slot 1)
    (operator pp-def 1)
    (operator pp-inline-label 1)
    (operator pp-items-cons 2)
    (operator pp-label 2)
    (operator pp-nonterminal 1)
    (operator pp-production 4)
    (operator pp-sub 2)
    (operator pp-terminal 1)
    (operator pp-terminal-char 1)
    (operator pp-terminal-class 1)
    (operator pp-transparent-inline-v1 1)
    (operator q-succ 1)
  )
  (equations)
  (rewrites
    (rule parser-pack-fact-467557577902ef5236e28c7a16f62887e4e3fa38d2af1047d049657931ec8fec
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 002dca985ccc91a15cfc4daacdf22873347644a103b808ea958bba45e829d4ea) (pp-def compressed-token-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body)) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil)) (pa-apply node (pa-cons (pa-const compressed-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-a6c7caa79e3a5231d14be1a099861f2d180ae52d41750a09a818c35a4ea8c5e1
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 00962cfd79fa659f8ab5141965275b5ce6467005381e906b3f565b2d459cf581) (pp-def kw-block-open) (pp-items-cons (pp-nonterminal (pp-def kw-block-open-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0b5c58204213c2211c8acd42ded33ca579b1412a3eefd08e773aa3cc926fa3b2
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 02c2e5eea5ba0ec8038c4400c555ec9ade37d2cf0446d4c54c36fdcc852b7f5c) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_const)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2a3b3512ed915ad6281688cf0faec43c18846ff3df598ea05992855b06e8dbbb
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 043b77fc1bea120a5aabdf9c7fff4e114970b32e438568e472145886b3c62dd9) (pp-def mm-production-symbols_one) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) pp-items-nil) (pa-apply node (pa-cons (pa-const symbols_one) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-c09dcc3f2598684e7364cea4c4d982fd3ea3527fdb3ff4d0ec4928b487fd3c00
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 0abe50caf284052b3857327b1ab64e1eccfde3e674dd4c472274c35cac43223e) (pp-def mm-production-statement_block) (pp-items-cons (pp-nonterminal (pp-def kw-block-open)) (pp-items-cons (pp-nonterminal (pp-def mm-category-database)) (pp-items-cons (pp-nonterminal (pp-def kw-block-close)) pp-items-nil))) (pa-apply node (pa-cons (pa-const statement_block) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-c9867246cde69e911d0eac86ada4a3dca214bcfead2a026da45751870e263dfb
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 0baf259713754f471dd2dc56081cc6c4a022064766f081af9a7e09be561899ed) (pp-def whitespace1) (pp-items-cons (pp-terminal (pp-terminal-class whitespace)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-d0092acc76ee711392afd91be9ca7e42e824dd21dd6a89dbe8c570772ffc4004
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 0bff27761f12f9144a9722a08c7a519a209adcedadbb70ba0bf2e50e0a3dd65d) (pp-def kw-theorem) (pp-items-cons (pp-nonterminal (pp-def kw-theorem-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-85afee832218c68e7ce391e673671b6e2e01431d32b8567c858ccbecaa84a68e
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 0f0175619bb40aa3fac7cc2f398d486123ed4afff332d2b8e8d2fb7f840a8fe9) (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-class math-symbol-char)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def include-token-lexeme) left) body)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-75883a2d7a409acf59c14d2c54026e769e1bd129dcca921a06e5a538e6507e9c
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 0f8a96e954fcb5ef995a474d63b7dbb4546995cfed8c143a79b8d3a621a98024) (pp-def left-paren) (pp-items-cons (pp-nonterminal (pp-def left-paren-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fd64679073a6e223bb1d102059bd9813e08fa7f718d0804f3be6683503432ea4
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 102a64ebe2f58d3a2934624287f3f279394eceb883542141dd5a962fcd22d165) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_theorem_compressed)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-257f1b38e8d1804f6811676c9bf9781e8f918d1135afeafcd06a8a688ee051f1
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 10359e6438f02aa67e4e08d52163fe1f097b48adec705b4e5ecdd6f92558baec) (pp-def mm-category-outer_statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_include)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f99618ba866d568f97f4512fb5f492ac77e6bee8ac0a8d95caf3d7b3ac053060
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 130e7cf2d37656e25c5c2cfe7445c61c663152751d24548b88d08407c8b3cf6f) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_disjoint)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-62f603db605ad5a9a02d2c18988cdeb2350eda80773cc3c6c0a0444090b134f7
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 141f345bb31f09e4e51f2f6089c6c0929579a2045601c3eb719245cfc57a069b) (pp-def mm-category-compressed_word_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-compressed_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-20eb3ed24bda73341332b1be0059a0543ecbe9659fe14e46706e3182d25e090d
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 18e9cf67631d2f430e7f69574ba6209881bcaf510f54ffff7b6dce5ef52e7ff9) (pp-def skip-unit) (pp-items-cons (pp-terminal (pp-terminal-class whitespace)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-8b8a4891f3c7e3f160387710bc10add9247ea6e645fc3cf83b035688cbc99a91
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 19d16cc515fa9f774678d653894ac47ad0e2be5a09cb7b2f590d60477a27a2ed) (pp-def mm-category-database) (pp-items-cons (pp-nonterminal (pp-def mm-production-database_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7794939c493f695d6a5e9204adfe8e5386b46d6b81a7998ec3f66b7a2ddf7a45
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 1e0baa3af9bd22f0a3e1e72e4bd72a55c4fd34de30a4d17e707b02bb1bda8950) (pp-def compressed-token) (pp-items-cons (pp-nonterminal (pp-def compressed-token-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ba6497800958b924cb3461f59ebd36e04874f4d7d4d241885b0eb9bf4e9135ec
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 2291d6a5278aa0cea78ca8f0ef07aad29c15da631dd77dc4c33be1affc16987b) (pp-def kw-block-close-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 125))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-eefd90d03fa3d8780fc6ec14142fe30e8dce6671f85fecdaa3ce75e28bf650ab
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 236f59685f3d39cac99d2a324e06ccc44b18f3ee775cb23214f2ab9a19dad76f) (pp-def mm-category-proof_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-proof_unknown_one)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-4e9e57553dad212c3e60c625fae81428c9a527bd77b0337f0a0396872766deb5
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 247a8bf6f9f013decefda496206675f29a1ec315dff2304c686edb13e66c3e7e) (pp-def mm-production-disjoint_symbols_more) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) (pp-items-cons (pp-nonterminal (pp-def mm-category-disjoint_symbol_list)) pp-items-nil)) (pa-apply node (pa-cons (pa-const disjoint_symbols_more) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-b040efdc46f655b218cdad00c1f11f9d9260629255a22cc6eb3ca0749139c5de
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 27928e2e7aad331106e3c2560d0ad6a41a2097ea6c76570ec0c263e018eb21ba) (pp-def symbol-token) (pp-items-cons (pp-nonterminal (pp-def symbol-token-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-19d2d075fb510316688163b5bd6c677dd5d6ba81bf58cad45b524072b194c2a0
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 2c942060cb3cb9bfa411ed66f1221954f15ee2c85d732eb2861ddabd4ce05978) (pp-def kw-disjoint-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 100))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-911e7211443c57131b66ba6ab407d27eb1bb2c2ba426668afeeee11c0b4e4d9c
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 2cc0f8250f8e6b1fd828d78ab2969901d5aac297e060c114b5c72defe1457389) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_block)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fa2d601e09800381a34d04b0459a26a905180926f35ceda3581589197dee551c
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 2df4c611cf78d2d0996bcd0f2f190cf4fc80e0779141ac2e95e1c0ff5a6cef8c) (pp-def kw-float-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 102))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-02b9babd63aab39e053e047fb16e239d1eaa56fd748d1103e68e2a56ac9e73f8
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-def mm-production-statement_float) (pp-items-cons (pp-nonterminal (pp-def label-token)) (pp-items-cons (pp-nonterminal (pp-def kw-float)) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil))))) (pa-apply node (pa-cons (pa-const statement_float) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-apply pair (pa-cons (pa-slot (q-succ (q-succ q-zero))) (pa-cons (pa-slot (q-succ (q-succ (q-succ q-zero)))) pa-nil))) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-7506a4e191dc7bc4e8a833df97bd215c847197663d2fe76e2b4939ff03a66ff9
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 385c4776f149bcb254a3de91559cbcf076a3041a605f2edb18aec469bd45fa6d) (pp-sub (pp-def comment-chunk) left) (pp-items-cons (pp-nonterminal (pp-def comment-pair-unit)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def comment-chunk) left)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-bf4743081d4b8457364a14276b4d50d67f393fb926133c8c45fbe59b9856602c
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 3a1eca614592aa23f1163a6b7ea4da1da6abe424c6e739d112865971d386fef5) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_theorem_normal)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-078310273623b12f70ac79362ea13f90931ad9846faac433fc0e748c5822ac30
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 3d45336bbf7a3983ec1cc1b0245250037b40ec6712be02d5b0666c68f476cadd) (pp-def kw-axiom) (pp-items-cons (pp-nonterminal (pp-def kw-axiom-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-10ab6ba6dd7308cbdb4d34a205402d412cba964991410d3aba48d337a8a7a90f
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 3e7c4590cbe51656a034bbe71566b5473c543baa98f2e4f8cc206f7abb14f1df) (pp-def proof-label-token-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body)) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil)) (pa-apply node (pa-cons (pa-const proof-label-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-25653eb888f04cef3ef149ea1fc44791f3d1f502c8d61286ff5c60dd4ffa556c
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 3f697edd94e8958e836c1509bdefff4cc095f973e30f76aa34fb64d9a179458a) (pp-def skip-unit) (pp-items-cons (pp-nonterminal (pp-def comment)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-4e098bb3b5fab5f4e87d063d4a4e82a532d9997e5bacbee83ade51485252df70
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 3f7e82d064a3e3177776eed310d8f559ef8b8ecfa46ed48191a3a33a1488ed8e) (pp-def mm-production-proof_unknown_more) (pp-items-cons (pp-nonterminal (pp-def unknown-proof)) (pp-items-cons (pp-nonterminal (pp-def mm-category-proof_list)) pp-items-nil)) (pa-apply node (pa-cons (pa-const proof_unknown_more) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-0520ca7acee5f66a76764d150fbbe8fa894ec4c9f9a2eca52f026f39614b524d
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 40a8fc6c5f1bd1b4fff29fc01d36c0c02b9a0caaea5cca96f461fd2116fc45ed) (pp-def mm-production-outer_database_empty) pp-items-nil (pa-apply node (pa-cons (pa-const outer_database_empty) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-d278eeedb94b4a53f66a59fb64c73eb578f7cd1b4790a805cac8ab61ec383a12
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 48b54cb65e282199132af41529c98c69655def088005db9c2e4c8735a7e00595) (pp-def kw-disjoint) (pp-items-cons (pp-nonterminal (pp-def kw-disjoint-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-98c2cf967b1522cc0a3298b48e0bcab49995f826fbaf9e8afd8d35a9596452d7
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 48d4c0b4f341ea7eb5eadaaaddae0591d42956a99fb0ff728dec3884d268458a) (pp-def mm-production-proof_unknown_one) (pp-items-cons (pp-nonterminal (pp-def unknown-proof)) pp-items-nil) (pa-apply node (pa-cons (pa-const proof_unknown_one) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-6bba097f59781a3ccefddfc8c6df968656c4ef50e83ea2b5ab073f2c6e160c5b
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 497a1c9a08a0db1c0757eb9f8f569e1b1ca2d5d602ae34749ff71c901d9df29d) (pp-def kw-include-open-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 91))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-1531b97680ce5a3bae7ebc4cde5eb45ddcec6772e84c3506c4b51a62eea5d23c
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 4a0b76ed2ebcd72851b2dcc8f3076580e4714c77cb0cacc1307dc85dbebee383) (pp-def kw-end) (pp-items-cons (pp-nonterminal (pp-def kw-end-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0f77f6d4d7da966f38cd8f383031291b0730ddd112556dbd67179072638c3eeb
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 4a81611cc535df379159b9f6bd9fe66fc54c6a851d6634dc61025cbf9b6cbd03) (pp-def mm-production-proof_more) (pp-items-cons (pp-nonterminal (pp-def proof-label-token)) (pp-items-cons (pp-nonterminal (pp-def mm-category-proof_list)) pp-items-nil)) (pa-apply node (pa-cons (pa-const proof_more) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-3b2ae2b1432593a122513253b92e49acd8a20d3fb6f2b4ec506a4e77cf967484
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 4cd14fad0bd4a6b00efa83ca124edce8209069323c54e3d7db34aba99c7b17df) (pp-def proof-label-token) (pp-items-cons (pp-nonterminal (pp-def proof-label-token-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-981093e1630b850656670e6f32a50a68e236905f1f53c39d1d1b4d73b893e3de
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 4d15101a28388cf0d9919a00ef50bff04f843139fb71291731d50bdf4376b093) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def comment-chunk)) (pp-items-cons (pp-nonterminal (pp-def whitespace1)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left)) pp-items-nil))) (pa-apply cons (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))) (pa-cons (pa-slot (q-succ (q-succ q-zero))) pa-nil))))))
      (body))
    (rule parser-pack-fact-0bd2da3bc39c8a1fc4a0a2d3cc43ccbf9f09d1b11332d67ba6142d92942abaec
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 4fdfe6fe05a42c9eaa4ca7e4b5af6fe8816ef838a4bc463fc0abca44c8d9d932) (pp-def mm-production-statement_var) (pp-items-cons (pp-nonterminal (pp-def kw-variable)) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil))) (pa-apply node (pa-cons (pa-const statement_var) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-2e9d7e5787f6cc32b3d9a659bac92a674a68119193cf6919ac737f2f71c90620
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 50363561cbc74f751bde3f25c0974103abe66616641c848c6d4dd099e2498475) (pp-def skip) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-7b54d59c444e1ff8162d5d1f3ad2eb27773d86015c76732ebad3fb76ced55e16
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 50adf1502184a6191d08c16496e705e952d4dc435ba86c8505d96148b91a776e) (pp-def kw-block-open-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 123))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-15f2d15cfc8ee55260bf8cfff794cbb17174de00c58ae3c7c36327689e32c6fb
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 50d9e195dfc653057bda9b02302219f80452f9a256576b413a14c7f719c1e910) (pp-def skip) (pp-items-cons (pp-nonterminal (pp-def skip-unit)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-d33fe05ca716757a0c40da4b7be0e50c5b67d6a5b7fa2a765a7e9cd5613ead60
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 513391a1b0c2389781214e3a2e14f0ae9ba39116033156abb9a7c280b3295398) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_var)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f6a6b2bbd8286c9417a39c67afe8bed7981e0aa4b82451a10e91969937a50902
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 53ea97c810f512a1180465264941005c56558d23f95bfb15eb19746f3c79b0a1) (pp-def kw-essential-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 101))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-0ee578a9c944848573d8d31b5528eef4b2e89d75adb11d4e70117c9cf861f19e
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 5492d695f002186d7bfc8361115fb6d978cced046418f1e54674dcdd86fffa21) (pp-def mm-category-symbol_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-symbols_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-1baf6a79e00082c4611c8515e6dbdd618a769d46ea0e40c98bd63a4d4fc51b89
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 585ea1c332f0a0b4e708a66b89b60f197a0ea1ca8764cbd51e06efa4b7a19a0b) (pp-def mm-category-outer_database) (pp-items-cons (pp-nonterminal (pp-def mm-production-outer_database_empty)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f99586022247f54c80b6023a79aa41804d1d831f31dbba8a51e54b35eab0c62e
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 58eabe6d56cfe6aabb3ded3e660a28e29cf9a5c4f843bd7e66b8bd5c8cb83592) (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-class math-symbol-char)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-3451f7d10b47929e5b22b64bda1b878c5a174171a9d8531ce6df95215c4b065a
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 5a4b7032651d1151858f8ff1377a2d66ccc8a9b97287c589d618e20af912a4bb) (pp-def left-paren-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 40))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-82a3c51f84ee57ed1848098573753b02460cb0b25c5868d001a27800f84f824e
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 5a74ae060295ad8f925c8b441bad0b4c44a39e7b686aef2e8f8f9e421d3aaf0d) (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-class math-symbol-char)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-f07ddc332eb24cfc29b8df7b1f82016b578eff251423131b7fdc260d30a79e62
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 5c87514f7ba36c1ef24291eab905cb355f9b43ae60069672e682b6bd9cbd338e) (pp-def mm-production-statement_const) (pp-items-cons (pp-nonterminal (pp-def kw-constant)) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil))) (pa-apply node (pa-cons (pa-const statement_const) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-6ca60fe1b2e9198a6f8b108d9b0855d27f4e88f49fd451643d7d1de9e5d24d0d
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 5ca9f417be471a2f03da5924375e9be37507d88585351c10c951991c04a31bc3) (pp-sub (pp-def comment-chunk) left) (pp-items-cons (pp-nonterminal (pp-def comment-pair-unit)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-8b1eae7d3e5825fa5bb4558fb4c1fb01259e0673b8ef4d528695ffe6f8693ff8
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 5e08f679362e22c908cd3c78cc6efcd2ed8dfb2ee77fb915a8a3a46e8611ceb7) (pp-sub (pp-sub (pp-def comment-chunk) right) left) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-1f249cced6635e894ff65b839fdcf1cbbe1ffa05a64b77f88ce51bb5d06adfb1
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 5ea94606b9f2b33d374458ac9ca9831737a41c601fae40427751e18df05993a0) (pp-def right-paren) (pp-items-cons (pp-nonterminal (pp-def right-paren-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-722c247c942558d2ee156dd123e6fe4b988c97507afe13ccc326c9af4fdfd6a5
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 5ff6283601680537577d2177ce846cbebebe87d3adf0aff49fd22b45c4b5a354) (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-class compressed-char)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-804e0b2430e4db7e15657e2ef431196e132892058025b6a7a81dcb3e812cb8d9
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-def mm-production-statement_theorem_compressed) (pp-items-cons (pp-nonterminal (pp-def label-token)) (pp-items-cons (pp-nonterminal (pp-def kw-theorem)) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) (pp-items-cons (pp-nonterminal (pp-def kw-proof)) (pp-items-cons (pp-nonterminal (pp-def left-paren)) (pp-items-cons (pp-nonterminal (pp-def mm-category-proof_header_list)) (pp-items-cons (pp-nonterminal (pp-def right-paren)) (pp-items-cons (pp-nonterminal (pp-def mm-category-compressed_word_list)) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil))))))))) (pa-apply node (pa-cons (pa-const statement_theorem_compressed) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-apply pair (pa-cons (pa-slot (q-succ (q-succ q-zero))) (pa-cons (pa-apply pair (pa-cons (pa-slot (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))) (pa-cons (pa-slot (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))))) pa-nil))) pa-nil))) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-d03edb6edd168f883588894c13974622aa5f663ca85c4d4cc5f8fe0b5978fe27
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 64221477dadfb3c84f2f3a038a40a618e08f2a2ca55d1ddfe2be2b054f50b326) (pp-def mm-category-proof_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-proof_one)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d5bdbd1032ed9801dd2063d73b20907ea1ddabd0f344d6578ce498d045c795be
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 67e650bd9426f86d6f5b9b3f1e449e055b73d9bf09fdae6f235bcd9afa3b8d61) (pp-def mm-production-proof_one) (pp-items-cons (pp-nonterminal (pp-def proof-label-token)) pp-items-nil) (pa-apply node (pa-cons (pa-const proof_one) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-bb7d35adaf8acd5e7363c56bc2a78b192a3bf46bc63952e8f8c78f2c88a5a643
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 6a66bb9b972338d1a483bbbe95c837716821346fc8d4e8a9e6fa8e7f62dbeaf9) (pp-def kw-include-close-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 93))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-856d21918c2cf94c6c26d8bddaa9b777e8361868ed04d5fb13ae5043986d35cc
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 6c45f3a69143a6c02cc760a06e85d0007b69323bd0ca59997e17ea09e074219c) (pp-def mm-category-proof_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-proof_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-67545fbce1f55eb0fa0737b0120ea4a0f0b801fda16771e73cce1274a471baca
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-def mm-production-statement_essential) (pp-items-cons (pp-nonterminal (pp-def label-token)) (pp-items-cons (pp-nonterminal (pp-def kw-essential)) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil)))) (pa-apply node (pa-cons (pa-const statement_essential) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ (q-succ q-zero))) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-d27a3cf35ef1a698985ef7db8dd6e08dfa69a64554b835b2b66af44faeaace5b
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 6e817207322d301fda5fb885394deb23fb2a6e7ea92907dc33d32a376628b60a) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-587dbfa8d62dfdaafbcff5c7013dc3158c321b7e167f0342f500e365e17e97fb
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 74233964f80bdcb1d726a9579d9fff99d8d063764a3d2755483ca239977cbaa6) (pp-def mm-category-proof_header_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-header_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-47441950bde394c9fe1c9075ccfc13f12dcb51f6e01e0382e51dc964cecd9c4f
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 757f57fdb2ebd4d8246caebe0aec8eb1ccc4d5d9dce9a88efcec7148de91bf74) (pp-def right-paren-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 41))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-3b646b05fbc6bafe69f37af2feb285f4c1a342e79a1e3e2e9a174ccc22d86232
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 7626ad4c39b98eb6f71599df437134185878c2a74a90ed31b11a27a5e8fe4046) (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-class label-char)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def label-token-lexeme) left) body)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-7739c57a1c9a329ceb7e3632ef5b0927ee95f0ac0ff31378631303bcc4f3133c
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 775e0c31c812cd8868036b6a2d953be810cce52108ed62a27a310749f68227f8) (pp-def mm-category-disjoint_symbol_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-disjoint_symbols_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-cd5ccd150d3f9f0620d8d428837bafb85557e4c6153a273a2f5b26a04f4ab156
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 78873b124e8d6e10dea0353f19cdae566493e793618ef0710676db077f0a5963) (pp-def mm-production-statement_disjoint) (pp-items-cons (pp-nonterminal (pp-def kw-disjoint)) (pp-items-cons (pp-nonterminal (pp-def mm-category-disjoint_symbol_list)) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil))) (pa-apply node (pa-cons (pa-const statement_disjoint) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-737f1849a15912b83f1f4278762cdc1759158788b6b397d83b160600e23a8bfd
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 7a22485051fbdb7f148150e005ce68a455e4b520873bcc93d2dc96f733c62221) (pp-def kw-block-close) (pp-items-cons (pp-nonterminal (pp-def kw-block-close-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a453e652506d2f7140970ef3ed7cd6a271be861153cefc9928eb9bd346503bb8
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-def mm-production-statement_theorem_normal) (pp-items-cons (pp-nonterminal (pp-def label-token)) (pp-items-cons (pp-nonterminal (pp-def kw-theorem)) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) (pp-items-cons (pp-nonterminal (pp-def kw-proof)) (pp-items-cons (pp-nonterminal (pp-def mm-category-proof_list)) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil)))))) (pa-apply node (pa-cons (pa-const statement_theorem_normal) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-apply pair (pa-cons (pa-slot (q-succ (q-succ q-zero))) (pa-cons (pa-slot (q-succ (q-succ (q-succ (q-succ q-zero))))) pa-nil))) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-c59e45cbd0e9989e6740d9364664637b167da8a1a8df48288bebdb51843542ab
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 7b159c3aebd5b2765c42d2238eb710613c13cf11772ee2274079d0c3441d2c04) (pp-def mm-category-outer_database) (pp-items-cons (pp-nonterminal (pp-def mm-production-outer_database_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ccbc18d2a6e427b8fe5006206c7ffd57b0f52be2ee413af903e1d3772537796b
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 7c27338890eeddfd445c5ff3d8fe2e05073bbcb3510eaa947892d546cc8fd958) (pp-def kw-include-open) (pp-items-cons (pp-nonterminal (pp-def kw-include-open-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7ab049df26e37b65f33a0777d8a9b030c9f5fe53120ec3a046eab9bad35744ae
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 8056acdd4be7de6976847e353beb7fd143c8f3a86ca81fd4e4eb88f262775da1) (pp-def mm-production-statement_include) (pp-items-cons (pp-nonterminal (pp-def kw-include-open)) (pp-items-cons (pp-nonterminal (pp-def include-token)) (pp-items-cons (pp-nonterminal (pp-def kw-include-close)) pp-items-nil))) (pa-apply node (pa-cons (pa-const statement_include) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-ef5f7b8dd37a0f79dccb070c165c9ba7e17e39e20648602fb4ac46acda1dfb49
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 80b3e65136fdd40ca15429c7cd5a63d07670d098ffb3a07a6f303002628d923c) (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-class label-char)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-4ef9e39b571bdd3684c54391a0b849a1648876bce0485fae6c50ad5efc010f0d
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 8119e68213f3810fcbc160444babd29c48cde1b77485d5021f3bbb300967da0b) (pp-def kw-axiom-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 97))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-86bd475eb52358ff1323f67ec844741055e5815d6d6fa702262619fa74c76c4c
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 835ce5c6f3b0d79698c65bc91ae525641ffe261c354bae27337afb4b9740df54) (pp-def comment) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 40))) (pp-items-cons (pp-nonterminal (pp-def whitespace1)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left)) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 41))) (pp-items-cons (pp-terminal (pp-terminal-class whitespace)) pp-items-nil))))))) (pa-apply node (pa-cons (pa-const comment) (pa-cons (pa-apply pair (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ (q-succ (q-succ (q-succ q-zero))))) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ (q-succ (q-succ (q-succ (q-succ q-zero)))))) (pa-cons (pa-const nil) pa-nil))) pa-nil))) (pa-cons (pa-slot (q-succ (q-succ (q-succ (q-succ (q-succ (q-succ q-zero))))))) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-8c1d4ffcc524cc25d18b28839ac9e295d80b16ce16fa6751f805a270b5e3e965
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 84a7aede4214797b8153a700c39ae3f3a9dbe3b15de32b560039fb5308f53a75) (pp-def mm-production-database_more) (pp-items-cons (pp-nonterminal (pp-def mm-category-statement)) (pp-items-cons (pp-nonterminal (pp-def mm-category-database)) pp-items-nil)) (pa-apply node (pa-cons (pa-const database_more) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-68c85d0b126f24a0ed65ea7da8e7816fff546fa2050e0e1252892dcf8e3bb73f
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 87d35a0547aded5420aa736507f7c55b872dec28baa7db2deb5f2c3c652bb218) (pp-def mm-production-header_empty) pp-items-nil (pa-apply node (pa-cons (pa-const header_empty) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-aa791a962f113309fe8674a372a8e9d004b210e601d4b06efacc9398ea3ad680
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 8c95483f8e630649b8aeebc42e985c914815b49dacbc71585fbf4f3c41eebcec) (pp-def mm-production-disjoint_symbols_two) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) pp-items-nil)) (pa-apply node (pa-cons (pa-const disjoint_symbols_two) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-82626db739a9facc1f865137ad0c732d842aaf08a88c57723ab900d1d51449c3
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 8dc17324b9979f4a098e568cc24a86d88a16752c3ac72390279906940ded66f5) (pp-def symbol-token-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body)) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil)) (pa-apply node (pa-cons (pa-const symbol-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-535f573c8828ccc2aecb2bbdb8c562fe04e429e963cdf59bf0c90bdc883cfe06
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 903ff10c953de70b529e5913ebd2103bffc652a345645e47a427613144f53214) (pp-def comment-chunk) (pp-items-cons (pp-nonterminal (pp-sub (pp-def comment-chunk) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f524a91466bf23e83f69f201b45bfadb66172f002fcf392fe0f25fe9c2aa4680
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 9129b74a03cc57054c3c7faf25dbf0fe39488705e94c06b34e2b963180066d4e) (pp-def mm-category-proof_header_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-header_empty)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e610a9d02d0d3ae5f0f4cb560df36881e253565139df8cd7411136b4cf442f3e
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 914a1426ce08abfe495d34f42406127cc0bf0bb2ddd21ea434c10e9f272f16af) (pp-def kw-float) (pp-items-cons (pp-nonterminal (pp-def kw-float-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-eb3ab2898d71b1118e516b579673901aae9b5666ce169b200bac7201e6e5b861
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 915c79d1994e22a3d6ea5760e7bbdc8fe255e195ff8fb381620b4d6e5c1c67d2) (pp-def kw-variable) (pp-items-cons (pp-nonterminal (pp-def kw-variable-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-3fa84fe7a0e218973b931eb3b9f43996d9a5f9d71787b8f16a9eb065f4067879
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 93e0b1f12ae701279a774f720b02e33033d0c795adc6a8799889d9f9851bb87f) (pp-sub (pp-sub (pp-def comment-chunk) right) left) (pp-items-cons (pp-nonterminal (pp-def comment-pair-unit)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment-chunk) right) left)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-b7883684f57df1191cf2d360d717cc428ff3e792c38e5871b425640215f2efcf
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 954944ccdbf468236a40f6b271b25d6aff7b748f6b5021a55d98f0f6f3718a87) (pp-def mm-production-outer_statement_inner) (pp-items-cons (pp-nonterminal (pp-def mm-category-statement)) pp-items-nil) (pa-apply node (pa-cons (pa-const outer_statement_inner) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-62e534b97260b07b75d35df5276ebf165c731e9152186886dcd164d8831f5126
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label 9e3b08388aec62654a8a6d42f0937ac1ecc5ac1ed2bfc7791a1d163d6c40c921) (pp-def include-token-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def include-token-lexeme) left) body)) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil)) (pa-apply node (pa-cons (pa-const include-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-33232ff7b3d3519e63e564b8cc258ceae56ddc975bc324b5a9dd03bdf6e3fc8e
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label a2c4ebb0fb8bcab6fabe6befb49bd9429c4f1a8a22065506e158501fdacd59ae) (pp-def unknown-proof-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 63))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil)) (pa-apply node (pa-cons (pa-const unknown-proof-token) (pa-cons (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-747c26ca94d0e1b12080a75c6314465507275a5992819bab0098239beadae601
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label a2d9b2ffe4fef0c566b39925d798d605c43e90057971a1d9eecf2d82f74bfdd3) (pp-def mm-category-database) (pp-items-cons (pp-nonterminal (pp-def mm-production-database_empty)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-41780838fc974014a092b345f9092b1bcaf897f00f2bf8e1c2e5f474157398a6
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label a3892be823f272114001b8be3eb1d727c17a59f2cf68c4b73592739eb6f66974) (pp-def kw-constant-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 99))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-f542bb566b5be97f700ddb0e9de8449e35f494a946352cc81269431568d8b200
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label a3ba37ab2788f0c6066347fcbdca296677c5752d777e0290106b98fe48b08978) (pp-def mm-production-compressed_more) (pp-items-cons (pp-nonterminal (pp-def compressed-token)) (pp-items-cons (pp-nonterminal (pp-def mm-category-compressed_word_list)) pp-items-nil)) (pa-apply node (pa-cons (pa-const compressed_more) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-9ed0379fb7b4ec43bef5db44425a08f8227df7d605f8be6687b136b5b57e9256
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label a4a13cdce0c49c52cd766aed66bd5236a6338416403259fdb4ef55edbfecf018) (pp-def comment-pair-unit) (pp-items-cons (pp-terminal (pp-terminal-class comment-non-dollar)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c5fb9465d855a179382e3c64a6024ed775d0bceb822ab81d8935cdb7c77f3d7e
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label a53a6bc0da2529e6af09fc975bc1c293c2cb1c84782beadf965f70bc0474a0e9) (pp-def mm-production-outer_database_more) (pp-items-cons (pp-nonterminal (pp-def mm-category-outer_statement)) (pp-items-cons (pp-nonterminal (pp-def mm-category-outer_database)) pp-items-nil)) (pa-apply node (pa-cons (pa-const outer_database_more) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-52e8f072e049f091d3b509ed3923c940d4e466005d2313a9e0d05a7df07dc020
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label a842fcf5959b6196102cac1830f7d01b073d49bae1be83fea6ed66d4cf4db776) (pp-def mm-production-compressed_one) (pp-items-cons (pp-nonterminal (pp-def compressed-token)) pp-items-nil) (pa-apply node (pa-cons (pa-const compressed_one) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-4c43c6b4dbc2a7434b4d1747a6a66e99058f28473a4264fb19917e101e0e63e7
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label acbd5f666242cfbb41329b2bdd3492dd49382ae36428667e1225ecec0a3d70b5) (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-class label-char)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-b9d4ba99802779f11b6b1022cc3ba316dee86db7cae526f84ba9c0005b86cbb7
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label b011bb353d6c63d8187758061817d36ee2365c36f3cb2cbeecb86f6ac81a3c96) (pp-def mm-category-disjoint_symbol_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-disjoint_symbols_two)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e24270bcbdb9896c36507f35846e67c536aaf2be3ad6a0e88fe38a299eeb9367
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label b1eea1f0963b6a43a1232f6f0c5b6bf14be7566cdd9f82f41315f9e725c78010) (pp-def whitespace1) (pp-items-cons (pp-terminal (pp-terminal-class whitespace)) (pp-items-cons (pp-nonterminal (pp-def whitespace1)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-d67a4bf0232a743050de086e15cb6e0bf4fa894f5f124b0f2b9c96d97f71dc55
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label b3a70fa6305b072264e07b9b82a0c2a0a48cc5ca9d5bf78940c4386c8ff28059) (pp-def mm-category-proof_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-proof_unknown_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e36944882fcb23cf825f0508b066e815cd92d3c74d770bb4eb2cb2db0a874a63
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label b43f7cb1f7d889d9e1ed810cb72cd7d1e6bf8b1933a487a37216a69b48bf2f09) (pp-def comment-chunk) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment-chunk) right) left)) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-74109ab2f587de8350bd2a743f4b8868c5475cf30d90337649a7905aa279ae96
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label b7d20d30bb3406c3a908ad66a195487984ce3c09a969f7d8d6af5644b9b4e4b5) (pp-def token-separator) (pp-items-cons (pp-terminal (pp-terminal-class whitespace)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-4ec77cbb96764ea3cb6bb33c790f5c5591b51b0d540cff31912879807ba9efb6
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label b9468f5bcb8a0858f1ae1155045025c7feee23cadbbf26744674bcf518770251) (pp-def mm-category-outer_statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-outer_statement_inner)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-90603a5e06c9a3380ef9b1d30f6dc41bbaeaec3b839225bc8ad3f6ae8ae436d1
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label bd50616733a528ed76c00f661497f7f343e83ec34478087411e360eee30ef653) (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-class compressed-char)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-7c4b71abf1ee16a54e1516351abe61a25dbc3c1423480ed0eecdca6c6aae013e
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label bedc5f31fb902cacf017b053814d05208ff6959017eaa9cad1c0b96a458fd0f4) (pp-def mm-production-database_empty) pp-items-nil (pa-apply node (pa-cons (pa-const database_empty) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-309336d2964baee45bd2c8384d1edbc4e519e08df1067c2348b1ec01880f8203
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label bfbdc59f73a802b8e94dc4efd299970e4b43e1d81b041de2fd8bb654b0b8e5b7) (pp-def kw-variable-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 118))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-a01ed547465ae035feea74fc70940e1acd810b8175b2285a14d6e4d097d46667
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-def mm-production-statement_axiom) (pp-items-cons (pp-nonterminal (pp-def label-token)) (pp-items-cons (pp-nonterminal (pp-def kw-axiom)) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil)))) (pa-apply node (pa-cons (pa-const statement_axiom) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ (q-succ q-zero))) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-88d6e3701f1a45c4e3515f68986a6443146ce944f5f8e1ee6c40eed9cb2df20e
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label c5f9c90109d2129deb0fe017e104af1894c596d980af20ffc7d039e0f55bb219) (pp-def kw-include-close) (pp-items-cons (pp-nonterminal (pp-def kw-include-close-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-5e3f3840aea6b3e56b2cd80cc513ac3fb66dee4fd75d18eafc9510f24721b53d
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label c76b3ffe99a01a8068a6b7eb165222bf82158c0ac178d95887618cb27f6496dd) (pp-def kw-proof) (pp-items-cons (pp-nonterminal (pp-def kw-proof-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b6a85fa38013161316a473c1402f51dd4929c8b1468da8c0dd4b9bcce171439a
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label c977563cbc95179722720b3e5bf4a5f2da56afbf18d02a936cf771f7fb2821cd) (pp-def kw-proof-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 61))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-7d4da3e7f540edf608e41eff5351a7860050fd9a116c6a6e2196ef827f0274a7
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label ca101022a915194182ca767fece26d79e3245d886570a7d118dd6e8c6c351475) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_axiom)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-4da1387d608f8baa9472325724a53ea1ae2ef4ff705bc23154ff9347fbd4a132
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label cb4b0d7c1e8a2602f3257baa96a31b8264f9d318bc23700d6521cd0d4c1da3b5) (pp-def kw-end-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 46))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-4788d55fea168b82c129f719b9c32b20a236415e07685aacefacef73b86fb11f
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label cea7c945352d1ddf47909cbe0cb1153a4a6a527fb6ada172aedb2d9224b5c4d0) (pp-def unknown-proof) (pp-items-cons (pp-nonterminal (pp-def unknown-proof-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-bf88c0fb273cb891867734ae264fb9f594be4d96c6a0b435e9ecc42a1430b2aa
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label d2ae8280fd00b82588ba9e6f862b4b2d7c964c2728c06672595c626a86929469) (pp-def kw-theorem-lexeme) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 112))) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil))) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-10f67c54d307d509e203e0ea9e4718bc04bf8f93f1792e5fdf362644fd4fb2df
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label d348b73eefd79e9a950622081bc3182fa5bea7a9cffec03df8f2679fc3f616c6) (pp-def label-token-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def label-token-lexeme) left) body)) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil)) (pa-apply node (pa-cons (pa-const label-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-147242e9b2a7655a30ab1c8c9bc86ae8f2c5d341da0044ab426ed74cf9544efe
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label d4d7dec498dbb322f5f1d48708800e142bfc8c223b07ac1db8317fbd2fe539a6) (pp-def mm-production-symbols_more) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) pp-items-nil)) (pa-apply node (pa-cons (pa-const symbols_more) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-3dc3b2349cc875ea95c4c2a87aea8e72dc83b01db4034ca9c5dd01098b804142
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label d961949d195259714ef1fdb5d5f07180a5bc25c11e4113f4a913abf83c592b69) (pp-def kw-constant) (pp-items-cons (pp-nonterminal (pp-def kw-constant-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fd70503b1e6b9f6fb713d1a59a3a6a4672caba1cd995b0a1cb375ca9fb0b6237
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label e73aa717c26f58dc410198f768bd7020a42445f425ca13901a660d71c21a7a20) (pp-def label-token) (pp-items-cons (pp-nonterminal (pp-def label-token-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0f96f1d42ac1ccfabd32522580c3b89c3fe66d7fff62e39bd371f7e8569de547
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label ee8fc33b21989bffe0b6cc70186ece9df106ab33196a30a847e1520ac1ebefd0) (pp-def token-separator) (pp-items-cons (pp-terminal pp-terminal-eof) pp-items-nil) (pa-const eof))))
      (body))
    (rule parser-pack-fact-285d263bb9475be6fbe6c6a4fd84ff141a90947e34d4b21bdff73197807b0765
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label ee9c4d513861c844edfbe77cb5c91188b407e6b3fc83c716fd1e3f094e17f2e9) (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-class label-char)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-7bb5b4adc1d8a5fbff536404c0010fe837397ed6c6b41a134cfab852a6ae7d08
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label f0cfd7736b6356f172e39753e7d4d256689ecd89af3ce34e2bee8d2e2e5059b6) (pp-def comment-pair-unit) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-class comment-dollar-follow)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-967ce3d510df6d8b0294cd70a5a250f2ee67709d9e631613e2048d28c6beb4b0
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label f0e62b1dcf6cbd1e02dfd3c3f88495e82fb4849ca455d3b86ea4d9eb238e9413) (pp-def include-token) (pp-items-cons (pp-nonterminal (pp-def include-token-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fe9a7790d9ff1a72fefa2e7cdf594b66dc3b0b51c4d6b94b884ab86b33b58a9b
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label f2b012df5229ba4e91d1208ee41d5ebfd75eda1e5c84aa2c9f59de9acab233c4) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_essential)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ef14cddc1690c301777b86fb320a370073f4f2835882d258f99cd29d246cb27a
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label f75c1a02a76ef66275a9f8d417557f9e76a750c7b682b9b83cccf69fdae8fb09) (pp-def database) (pp-items-cons (pp-nonterminal (pp-def skip)) (pp-items-cons (pp-nonterminal (pp-def mm-category-outer_database)) (pp-items-cons (pp-terminal pp-terminal-eof) pp-items-nil))) (pa-apply node (pa-cons (pa-const database) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-f1da53e8163713e8c820de4e76a9cb0d0ef49d313b7a6cf655b7cf17e78f1e22
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label facb0b5e476539290231dd2c120023f11fab01abb666b0c3dde3f5fb31638121) (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-class math-symbol-char)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-1a4f4e4e962c6a03ff89b3411a71412649d985f73b0418caa52dd2d62546f2d8
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label fb2a50d52cd37c4c436af699d0240736fede9ce456c121ea47f31e2264fe633e) (pp-def mm-category-compressed_word_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-compressed_one)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-8ad82a71fc5ccad24d6c4d079dfc70b7607fdf40b01b0684b1cd97ee22a359b5
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label fc5624e290af249b88c6eab39475466fa15d7c4fc3fb297d8001ff00fa7a4aa3) (pp-def mm-category-symbol_list) (pp-items-cons (pp-nonterminal (pp-def mm-production-symbols_one)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b6892bab508a48dccf0bbec39e628e4b4cb0a4ddcaf39160e5e87e3be969b40c
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label fe786a044b8a590ab3bbe410c77a543e5da3f8357e6a298f6daadee652cb0369) (pp-def kw-essential) (pp-items-cons (pp-nonterminal (pp-def kw-essential-lexeme)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d8a0cbefcbe05b97d34a14bad2c928496d285c4c7e323dc323fdbbb07292fe42
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label ff63cc9a997bf1a5736ff303e1b6ce090368b2f6bbe951cd0e04e7ef7411a27c) (pp-def mm-production-header_more) (pp-items-cons (pp-nonterminal (pp-def proof-label-token)) (pp-items-cons (pp-nonterminal (pp-def mm-category-proof_header_list)) pp-items-nil)) (pa-apply node (pa-cons (pa-const header_more) (pa-cons (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-576a848ab5c2a1b049f9f099ad75e41c151c20c611e207f66356887e79ae1a1a
      (head (compile-pack-production (pp-transparent-inline-v1 f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca) (pp-production (pp-inline-label ffa64c09911433009ca6ca56003e28054938d5043aab14ef46b5829949c4bf48) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_float)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2f6d99226444311826c1cbacd10b693e6abef93e54d01f0d28077949fda0f070
      (head (compile-pack-source-occurrence (pp-inline-label 002dca985ccc91a15cfc4daacdf22873347644a103b808ea958bba45e829d4ea) (pp-label (pp-def compressed-token-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-e332fa894a75a102b808b9dfde17addc96233d64c8a2fe463b1599fe167eb589
      (head (compile-pack-source-occurrence (pp-inline-label 002dca985ccc91a15cfc4daacdf22873347644a103b808ea958bba45e829d4ea) (pp-label (pp-sub (pp-def compressed-token-lexeme) left) node) 1))
      (body))
    (rule parser-pack-fact-bf59dfea970ef8bede95573fe68944f8931969b6b5b8e442423162252f0bbc50
      (head (compile-pack-source-occurrence (pp-inline-label 002dca985ccc91a15cfc4daacdf22873347644a103b808ea958bba45e829d4ea) (pp-label (pp-sub (pp-def compressed-token-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-511b74b0bf4d1bb57326cc1d425f9eb5662d00891e494abbd1cdd1fb4a9d8083
      (head (compile-pack-source-occurrence (pp-inline-label 00962cfd79fa659f8ab5141965275b5ce6467005381e906b3f565b2d459cf581) (pp-label (pp-def kw-block-open) left) 0))
      (body))
    (rule parser-pack-fact-53da74dc0c3b3f37d657319aea76e755679c44cb6de3597b65f62e7393338656
      (head (compile-pack-source-occurrence (pp-inline-label 00962cfd79fa659f8ab5141965275b5ce6467005381e906b3f565b2d459cf581) (pp-label (pp-sub (pp-def kw-block-open) left) ref) 1))
      (body))
    (rule parser-pack-fact-1c735a2aa9c8f251e254d4b0f85ead88ab0a2617ac9656818a90c71f2eecf576
      (head (compile-pack-source-occurrence (pp-inline-label 00962cfd79fa659f8ab5141965275b5ce6467005381e906b3f565b2d459cf581) (pp-label (pp-sub (pp-def kw-block-open) right) ref) 2))
      (body))
    (rule parser-pack-fact-556066ba319f102c855162852a29da00d4ca1e5a803b14422e193311d943476f
      (head (compile-pack-source-occurrence (pp-inline-label 02c2e5eea5ba0ec8038c4400c555ec9ade37d2cf0446d4c54c36fdcc852b7f5c) (pp-label (pp-def mm-category-statement) alt-left) 0))
      (body))
    (rule parser-pack-fact-09de59025991974cec51309d5d366219de11ea4629da875562fc2f7742d063bc
      (head (compile-pack-source-occurrence (pp-inline-label 02c2e5eea5ba0ec8038c4400c555ec9ade37d2cf0446d4c54c36fdcc852b7f5c) (pp-label (pp-sub (pp-def mm-category-statement) left) ref) 1))
      (body))
    (rule parser-pack-fact-5b497bf699a90efa715160550ce1c3050804211c01d51de75669762cd619928d
      (head (compile-pack-source-occurrence (pp-inline-label 043b77fc1bea120a5aabdf9c7fff4e114970b32e438568e472145886b3c62dd9) (pp-label (pp-def mm-production-symbols_one) node) 0))
      (body))
    (rule parser-pack-fact-6b5ec3fde6a68c149171eacfcd29e1e78bb930311214dc605f2bb0eaf30c54ec
      (head (compile-pack-source-occurrence (pp-inline-label 043b77fc1bea120a5aabdf9c7fff4e114970b32e438568e472145886b3c62dd9) (pp-label (pp-sub (pp-def mm-production-symbols_one) body) left) 1))
      (body))
    (rule parser-pack-fact-edd32cdbc426b9a71df045bdb62c4398cd059d71828003f1d1c5e8c5278aeab3
      (head (compile-pack-source-occurrence (pp-inline-label 043b77fc1bea120a5aabdf9c7fff4e114970b32e438568e472145886b3c62dd9) (pp-label (pp-sub (pp-sub (pp-def mm-production-symbols_one) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-784a047d4a83d9f474ff655a9cf74cc2b6d3111d345c00505fbe98d1dd9f4748
      (head (compile-pack-source-occurrence (pp-inline-label 043b77fc1bea120a5aabdf9c7fff4e114970b32e438568e472145886b3c62dd9) (pp-label (pp-sub (pp-sub (pp-def mm-production-symbols_one) body) right) eps) 3))
      (body))
    (rule parser-pack-fact-6a514941a3d3038bd3350cfefd681c2c0a4df474be98a153b8bdd3f6abe2d3b0
      (head (compile-pack-source-occurrence (pp-inline-label 0abe50caf284052b3857327b1ab64e1eccfde3e674dd4c472274c35cac43223e) (pp-label (pp-def mm-production-statement_block) node) 0))
      (body))
    (rule parser-pack-fact-30c6dcf0b1b4a9f699c107a28e1a1a62d53e9b4bb1b26e5aa6b4b81c9fec4155
      (head (compile-pack-source-occurrence (pp-inline-label 0abe50caf284052b3857327b1ab64e1eccfde3e674dd4c472274c35cac43223e) (pp-label (pp-sub (pp-def mm-production-statement_block) body) right) 1))
      (body))
    (rule parser-pack-fact-7b6bfa769c89dde65d3fac2c428c5fbf8cffa0bb68c41662caeea9029657d7da
      (head (compile-pack-source-occurrence (pp-inline-label 0abe50caf284052b3857327b1ab64e1eccfde3e674dd4c472274c35cac43223e) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_block) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-181e0422733ec1569716d25e18ddc59a9194007c89a018fcbb55904c42ccfcdb
      (head (compile-pack-source-occurrence (pp-inline-label 0abe50caf284052b3857327b1ab64e1eccfde3e674dd4c472274c35cac43223e) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) left) 3))
      (body))
    (rule parser-pack-fact-da4510807754d4679b80f45eaa2ea66d4ff81f79401dfd253c276b8641597ab0
      (head (compile-pack-source-occurrence (pp-inline-label 0abe50caf284052b3857327b1ab64e1eccfde3e674dd4c472274c35cac43223e) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-74894f2d4c54b1891ecfa6b9ac7f36b711f3ae51e5478a6b4b8aed6547f2ecf6
      (head (compile-pack-source-occurrence (pp-inline-label 0abe50caf284052b3857327b1ab64e1eccfde3e674dd4c472274c35cac43223e) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) right) 5))
      (body))
    (rule parser-pack-fact-58b3ad0ab9b0f0742f31b3295e87d2c694803372d2ea9654e995dc789e871697
      (head (compile-pack-source-occurrence (pp-inline-label 0abe50caf284052b3857327b1ab64e1eccfde3e674dd4c472274c35cac43223e) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-b20f0df306194ef38179001edc899b1c2f8122d92cf4846ab01a3030cb2e3392
      (head (compile-pack-source-occurrence (pp-inline-label 0abe50caf284052b3857327b1ab64e1eccfde3e674dd4c472274c35cac43223e) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) right) eps) 7))
      (body))
    (rule parser-pack-fact-16691d9459d7c1c45580a14b520af342174dbaa1214daa545edaebfd9d651fa5
      (head (compile-pack-source-occurrence (pp-inline-label 0baf259713754f471dd2dc56081cc6c4a022064766f081af9a7e09be561899ed) (pp-label (pp-def whitespace1) plus-one) 0))
      (body))
    (rule parser-pack-fact-4e2f6138a27315ecb2ccf56781fe3b2f50c99ae35eb02bb5bd55514729326ec9
      (head (compile-pack-source-occurrence (pp-inline-label 0baf259713754f471dd2dc56081cc6c4a022064766f081af9a7e09be561899ed) (pp-label (pp-sub (pp-def whitespace1) body) class) 1))
      (body))
    (rule parser-pack-fact-81a98c4f26c4b89d961c5f457c2e52b352ec46e357c510528cc7790ceb4711da
      (head (compile-pack-source-occurrence (pp-inline-label 0bff27761f12f9144a9722a08c7a519a209adcedadbb70ba0bf2e50e0a3dd65d) (pp-label (pp-def kw-theorem) left) 0))
      (body))
    (rule parser-pack-fact-c4e083719096b17a48c36591fed476b2386c1d382bd8573250d3ef9326d33b51
      (head (compile-pack-source-occurrence (pp-inline-label 0bff27761f12f9144a9722a08c7a519a209adcedadbb70ba0bf2e50e0a3dd65d) (pp-label (pp-sub (pp-def kw-theorem) left) ref) 1))
      (body))
    (rule parser-pack-fact-7cb3c55c733cf9ac38d671db43b56c65b16c4c1e7e000fe3fd641d9418e48fcf
      (head (compile-pack-source-occurrence (pp-inline-label 0bff27761f12f9144a9722a08c7a519a209adcedadbb70ba0bf2e50e0a3dd65d) (pp-label (pp-sub (pp-def kw-theorem) right) ref) 2))
      (body))
    (rule parser-pack-fact-880c3c6b3f2db545dd1c09cb282079aa09537d16e35dd609ad71ee57dbc2d678
      (head (compile-pack-source-occurrence (pp-inline-label 0f0175619bb40aa3fac7cc2f398d486123ed4afff332d2b8e8d2fb7f840a8fe9) (pp-label (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) plus-more) 0))
      (body))
    (rule parser-pack-fact-7db13e1996ac5944dc5764611db23d3e66b4693c9e27c0d79cdcd7d1cb8aeede
      (head (compile-pack-source-occurrence (pp-inline-label 0f0175619bb40aa3fac7cc2f398d486123ed4afff332d2b8e8d2fb7f840a8fe9) (pp-label (pp-sub (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) body) class) 1))
      (body))
    (rule parser-pack-fact-89e16125eed2a99eb6e5c61f24a193fd551c4fcfbca88e9216bc428cd4b9ad7c
      (head (compile-pack-source-occurrence (pp-inline-label 0f8a96e954fcb5ef995a474d63b7dbb4546995cfed8c143a79b8d3a621a98024) (pp-label (pp-def left-paren) left) 0))
      (body))
    (rule parser-pack-fact-456fe42af8c678a0597c60266034c3cf06060d2d45676fed971a775f4c823e0e
      (head (compile-pack-source-occurrence (pp-inline-label 0f8a96e954fcb5ef995a474d63b7dbb4546995cfed8c143a79b8d3a621a98024) (pp-label (pp-sub (pp-def left-paren) left) ref) 1))
      (body))
    (rule parser-pack-fact-410eef26b5aba009b968c3a8d32ca120521d8aa24aa787f32fcbbf3f4999ba28
      (head (compile-pack-source-occurrence (pp-inline-label 0f8a96e954fcb5ef995a474d63b7dbb4546995cfed8c143a79b8d3a621a98024) (pp-label (pp-sub (pp-def left-paren) right) ref) 2))
      (body))
    (rule parser-pack-fact-9fc34830e16b12822bb932bb0f3f0c544d12415135dc3692ba56925d7a5ae64c
      (head (compile-pack-source-occurrence (pp-inline-label 102a64ebe2f58d3a2934624287f3f279394eceb883542141dd5a962fcd22d165) (pp-label (pp-def mm-category-statement) alt-right) 0))
      (body))
    (rule parser-pack-fact-6acf3d10437e6db93cac97ec32dd82d2fec25f2b497baca6e1df4f5c3aeba09a
      (head (compile-pack-source-occurrence (pp-inline-label 102a64ebe2f58d3a2934624287f3f279394eceb883542141dd5a962fcd22d165) (pp-label (pp-sub (pp-def mm-category-statement) right) alt-right) 1))
      (body))
    (rule parser-pack-fact-f7dfd2ae2ce452135c6aa7ee4547b0121a83e7bd7e302ef0f735564bdcc5d96c
      (head (compile-pack-source-occurrence (pp-inline-label 102a64ebe2f58d3a2934624287f3f279394eceb883542141dd5a962fcd22d165) (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) right) alt-right) 2))
      (body))
    (rule parser-pack-fact-12b14b0b05fe7510ad174a7f2fc6e8f5eee9abd798afc426d8a30ffa047804cd
      (head (compile-pack-source-occurrence (pp-inline-label 102a64ebe2f58d3a2934624287f3f279394eceb883542141dd5a962fcd22d165) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) alt-right) 3))
      (body))
    (rule parser-pack-fact-7fb74131ae85033760d31ce4a19a437cccb53b2ad8226fb4be4e3adb24c2f16c
      (head (compile-pack-source-occurrence (pp-inline-label 102a64ebe2f58d3a2934624287f3f279394eceb883542141dd5a962fcd22d165) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) alt-right) 4))
      (body))
    (rule parser-pack-fact-ae034ec75917d2bb7266492ec1bdabb6c44e7b3f028430ff2a3a0951c1da84dc
      (head (compile-pack-source-occurrence (pp-inline-label 102a64ebe2f58d3a2934624287f3f279394eceb883542141dd5a962fcd22d165) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) alt-right) 5))
      (body))
    (rule parser-pack-fact-54b461c2b357394da6191177fa070a53afc178fc29b8b0a752821c6f86998e41
      (head (compile-pack-source-occurrence (pp-inline-label 102a64ebe2f58d3a2934624287f3f279394eceb883542141dd5a962fcd22d165) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) alt-right) 6))
      (body))
    (rule parser-pack-fact-7f0006bea3de18f0cc74e9351d8a48b2216bafcd2bdbaa645d7b4ddc569644e8
      (head (compile-pack-source-occurrence (pp-inline-label 102a64ebe2f58d3a2934624287f3f279394eceb883542141dd5a962fcd22d165) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) alt-left) 7))
      (body))
    (rule parser-pack-fact-ba04809be5699b0f635d7d140fa30abb1be8f8982f6fb6f22876b5fe0d8c0ae0
      (head (compile-pack-source-occurrence (pp-inline-label 102a64ebe2f58d3a2934624287f3f279394eceb883542141dd5a962fcd22d165) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) left) ref) 8))
      (body))
    (rule parser-pack-fact-0c896795951fd1f751e38336feb14048d5e336cc282a4871c7673c40fe023024
      (head (compile-pack-source-occurrence (pp-inline-label 10359e6438f02aa67e4e08d52163fe1f097b48adec705b4e5ecdd6f92558baec) (pp-label (pp-def mm-category-outer_statement) alt-left) 0))
      (body))
    (rule parser-pack-fact-8e45aa56ef54aaa21ba7ffa1321875030707e1e86d7d977a170beaadf68135f3
      (head (compile-pack-source-occurrence (pp-inline-label 10359e6438f02aa67e4e08d52163fe1f097b48adec705b4e5ecdd6f92558baec) (pp-label (pp-sub (pp-def mm-category-outer_statement) left) ref) 1))
      (body))
    (rule parser-pack-fact-c14a57228537fa23703dea18e7c95dbc1c20a964712cfb947c8065b66f660cb8
      (head (compile-pack-source-occurrence (pp-inline-label 130e7cf2d37656e25c5c2cfe7445c61c663152751d24548b88d08407c8b3cf6f) (pp-label (pp-def mm-category-statement) alt-right) 0))
      (body))
    (rule parser-pack-fact-cb8201a998d3cf60d620e7e26b785594a5f4cc558bdffd29f7e35cfde21f4f83
      (head (compile-pack-source-occurrence (pp-inline-label 130e7cf2d37656e25c5c2cfe7445c61c663152751d24548b88d08407c8b3cf6f) (pp-label (pp-sub (pp-def mm-category-statement) right) alt-right) 1))
      (body))
    (rule parser-pack-fact-a318a8efd9411a4cae7536a3f365f9ff94017a0714fce2186226805ba438b2d5
      (head (compile-pack-source-occurrence (pp-inline-label 130e7cf2d37656e25c5c2cfe7445c61c663152751d24548b88d08407c8b3cf6f) (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) right) alt-left) 2))
      (body))
    (rule parser-pack-fact-5715784ea93b5b600f7a3a45ff936b26b4d280977261c03f6bcef0295e4480ad
      (head (compile-pack-source-occurrence (pp-inline-label 130e7cf2d37656e25c5c2cfe7445c61c663152751d24548b88d08407c8b3cf6f) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) left) ref) 3))
      (body))
    (rule parser-pack-fact-35af2461853a49c0592e68385d5f8cbf425007daa21ccfd3cb6fb10b02d0631e
      (head (compile-pack-source-occurrence (pp-inline-label 141f345bb31f09e4e51f2f6089c6c0929579a2045601c3eb719245cfc57a069b) (pp-label (pp-def mm-category-compressed_word_list) alt-right) 0))
      (body))
    (rule parser-pack-fact-4d965fd4842df4c804c4c6ef5d60ec41e2277a8cb38fc3215b47850c435f792b
      (head (compile-pack-source-occurrence (pp-inline-label 141f345bb31f09e4e51f2f6089c6c0929579a2045601c3eb719245cfc57a069b) (pp-label (pp-sub (pp-def mm-category-compressed_word_list) right) ref) 1))
      (body))
    (rule parser-pack-fact-25bf8f2b06a4c5bcd831a7594da0f7e18c4a6da365c8ce85eaccefd93018e5fb
      (head (compile-pack-source-occurrence (pp-inline-label 18e9cf67631d2f430e7f69574ba6209881bcaf510f54ffff7b6dce5ef52e7ff9) (pp-label (pp-def skip-unit) alt-left) 0))
      (body))
    (rule parser-pack-fact-41849b240c9c68e2d971a1a4a9da70fd23a87186f0467f316024c358d28dcf4d
      (head (compile-pack-source-occurrence (pp-inline-label 18e9cf67631d2f430e7f69574ba6209881bcaf510f54ffff7b6dce5ef52e7ff9) (pp-label (pp-sub (pp-def skip-unit) left) class) 1))
      (body))
    (rule parser-pack-fact-483fbabf9ccaf9552bfe8b2de54e27544e6a7f428306f21d8ffa1b62e5d965e2
      (head (compile-pack-source-occurrence (pp-inline-label 19d16cc515fa9f774678d653894ac47ad0e2be5a09cb7b2f590d60477a27a2ed) (pp-label (pp-def mm-category-database) alt-right) 0))
      (body))
    (rule parser-pack-fact-1c0731debc955b62910541b57f05834bc421d7cd0dd5fe86702d7839fd3ec895
      (head (compile-pack-source-occurrence (pp-inline-label 19d16cc515fa9f774678d653894ac47ad0e2be5a09cb7b2f590d60477a27a2ed) (pp-label (pp-sub (pp-def mm-category-database) right) ref) 1))
      (body))
    (rule parser-pack-fact-3e2c625dc77b4e6843c8520d8bc4d7eeb7fccd85d7e5d1310f504395dcec759c
      (head (compile-pack-source-occurrence (pp-inline-label 1e0baa3af9bd22f0a3e1e72e4bd72a55c4fd34de30a4d17e707b02bb1bda8950) (pp-label (pp-def compressed-token) left) 0))
      (body))
    (rule parser-pack-fact-932b85cfde99cc1282c37d13b9f3cf25d4dcf0efab7e8874625881237df6ec3a
      (head (compile-pack-source-occurrence (pp-inline-label 1e0baa3af9bd22f0a3e1e72e4bd72a55c4fd34de30a4d17e707b02bb1bda8950) (pp-label (pp-sub (pp-def compressed-token) left) ref) 1))
      (body))
    (rule parser-pack-fact-0ceca7dbe62f319d93c6fa606f491bcd19a7ebe62187e7e4c328ff0a9cb1ad1b
      (head (compile-pack-source-occurrence (pp-inline-label 1e0baa3af9bd22f0a3e1e72e4bd72a55c4fd34de30a4d17e707b02bb1bda8950) (pp-label (pp-sub (pp-def compressed-token) right) ref) 2))
      (body))
    (rule parser-pack-fact-40baec174e4ca5451e5307dc2e4d9249b0c6a2b3de79554f7cd88a226dae0390
      (head (compile-pack-source-occurrence (pp-inline-label 2291d6a5278aa0cea78ca8f0ef07aad29c15da631dd77dc4c33be1affc16987b) (pp-label (pp-def kw-block-close-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-29fb29f1a5233801df411c8d03dea668fa2e04d7c13fcd160f15f4cff8bd84b9
      (head (compile-pack-source-occurrence (pp-inline-label 2291d6a5278aa0cea78ca8f0ef07aad29c15da631dd77dc4c33be1affc16987b) (pp-label (pp-sub (pp-def kw-block-close-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-3d87efc30d8687a5d62a8dad2f664738ba1e4bb279eef29ef1d55eecc139ab64
      (head (compile-pack-source-occurrence (pp-inline-label 2291d6a5278aa0cea78ca8f0ef07aad29c15da631dd77dc4c33be1affc16987b) (pp-label (pp-sub (pp-def kw-block-close-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-cd136be838d36addb9d7b5fa7acda8d39b0f58a4d2d1c93efba7e006376bfca2
      (head (compile-pack-source-occurrence (pp-inline-label 236f59685f3d39cac99d2a324e06ccc44b18f3ee775cb23214f2ab9a19dad76f) (pp-label (pp-def mm-category-proof_list) alt-right) 0))
      (body))
    (rule parser-pack-fact-67f7e9465bab06a12a55d629c6da54b1afc530893169896e50822e862f2eae7e
      (head (compile-pack-source-occurrence (pp-inline-label 236f59685f3d39cac99d2a324e06ccc44b18f3ee775cb23214f2ab9a19dad76f) (pp-label (pp-sub (pp-def mm-category-proof_list) right) alt-right) 1))
      (body))
    (rule parser-pack-fact-7f16acda39ca49332f077c9747fa32e2e0bdda990aaa60d7e234ae6120232802
      (head (compile-pack-source-occurrence (pp-inline-label 236f59685f3d39cac99d2a324e06ccc44b18f3ee775cb23214f2ab9a19dad76f) (pp-label (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) alt-left) 2))
      (body))
    (rule parser-pack-fact-b5137897823216d06127e6c971bf7186b273b3a17399524e7f51738fe763febf
      (head (compile-pack-source-occurrence (pp-inline-label 236f59685f3d39cac99d2a324e06ccc44b18f3ee775cb23214f2ab9a19dad76f) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) left) ref) 3))
      (body))
    (rule parser-pack-fact-dd7c68288e3252f57a555d548cd6f8dc7b47bfcfe8d4a410f1bfc7b12c0d953c
      (head (compile-pack-source-occurrence (pp-inline-label 247a8bf6f9f013decefda496206675f29a1ec315dff2304c686edb13e66c3e7e) (pp-label (pp-def mm-production-disjoint_symbols_more) node) 0))
      (body))
    (rule parser-pack-fact-e7f8e2a1f75e2652174582ec2834bbfa229f5b1ef340626bce774d49f28ae727
      (head (compile-pack-source-occurrence (pp-inline-label 247a8bf6f9f013decefda496206675f29a1ec315dff2304c686edb13e66c3e7e) (pp-label (pp-sub (pp-def mm-production-disjoint_symbols_more) body) seq) 1))
      (body))
    (rule parser-pack-fact-e346f25e62f16c50046c60b77bc07d24b4f171ece186be59cf4bdb1ddf07e844
      (head (compile-pack-source-occurrence (pp-inline-label 247a8bf6f9f013decefda496206675f29a1ec315dff2304c686edb13e66c3e7e) (pp-label (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-9fad07832a5d4d464387ac65c4e72b4ba269b289dc90f28566af7e35696f5648
      (head (compile-pack-source-occurrence (pp-inline-label 247a8bf6f9f013decefda496206675f29a1ec315dff2304c686edb13e66c3e7e) (pp-label (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) left) 3))
      (body))
    (rule parser-pack-fact-1fc5c34f706291f293132cc204b374c34051d765ac904cb710893dd696422f42
      (head (compile-pack-source-occurrence (pp-inline-label 247a8bf6f9f013decefda496206675f29a1ec315dff2304c686edb13e66c3e7e) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-980ffd0cccbcc0a87c86e821766d89bcf603b5f6e48c0c79cb2ce10c8b7adc8e
      (head (compile-pack-source-occurrence (pp-inline-label 247a8bf6f9f013decefda496206675f29a1ec315dff2304c686edb13e66c3e7e) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) right) eps) 5))
      (body))
    (rule parser-pack-fact-b491f7ec2ad3d7350c5bcf50e7b6d4bf4b537f2b1ab97b91b8959816f148ffec
      (head (compile-pack-source-occurrence (pp-inline-label 27928e2e7aad331106e3c2560d0ad6a41a2097ea6c76570ec0c263e018eb21ba) (pp-label (pp-def symbol-token) left) 0))
      (body))
    (rule parser-pack-fact-33fd02493c1f656e296921dcab989886ab32f03a760c38e1ba2e5fd71616cd52
      (head (compile-pack-source-occurrence (pp-inline-label 27928e2e7aad331106e3c2560d0ad6a41a2097ea6c76570ec0c263e018eb21ba) (pp-label (pp-sub (pp-def symbol-token) left) ref) 1))
      (body))
    (rule parser-pack-fact-1e0acb1f21299d4a68f9380442621a76de45e58c5aa519bc2a421b4350973c20
      (head (compile-pack-source-occurrence (pp-inline-label 27928e2e7aad331106e3c2560d0ad6a41a2097ea6c76570ec0c263e018eb21ba) (pp-label (pp-sub (pp-def symbol-token) right) ref) 2))
      (body))
    (rule parser-pack-fact-90add0057f52409401e36261ce0c1e0a6fa9247037dcf5a5cc06c1a38c5ef3c2
      (head (compile-pack-source-occurrence (pp-inline-label 2c942060cb3cb9bfa411ed66f1221954f15ee2c85d732eb2861ddabd4ce05978) (pp-label (pp-def kw-disjoint-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-719a4b802fe4f2fa91ed4d66c81afc2fd6001ac374173c69f646a1ab9dbf0592
      (head (compile-pack-source-occurrence (pp-inline-label 2c942060cb3cb9bfa411ed66f1221954f15ee2c85d732eb2861ddabd4ce05978) (pp-label (pp-sub (pp-def kw-disjoint-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-615482b7d36b8c0bbd92291ed06a25d33590e7d0e9ecd4de1ff03091cbb49c50
      (head (compile-pack-source-occurrence (pp-inline-label 2c942060cb3cb9bfa411ed66f1221954f15ee2c85d732eb2861ddabd4ce05978) (pp-label (pp-sub (pp-def kw-disjoint-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-6f79067fa433563cfe9116fa27bb064bd1286a7082d6e9413871f6635b4af811
      (head (compile-pack-source-occurrence (pp-inline-label 2cc0f8250f8e6b1fd828d78ab2969901d5aac297e060c114b5c72defe1457389) (pp-label (pp-def mm-category-statement) alt-right) 0))
      (body))
    (rule parser-pack-fact-16a13fa3fc2056b74e34410a3d4bfd7cab4605fa952da76a0d7ee89778779715
      (head (compile-pack-source-occurrence (pp-inline-label 2cc0f8250f8e6b1fd828d78ab2969901d5aac297e060c114b5c72defe1457389) (pp-label (pp-sub (pp-def mm-category-statement) right) alt-right) 1))
      (body))
    (rule parser-pack-fact-b0abe81e05ccabc93ac7b6b6b01b0ab50c436a9ec0937be0c430da864ad4d6ea
      (head (compile-pack-source-occurrence (pp-inline-label 2cc0f8250f8e6b1fd828d78ab2969901d5aac297e060c114b5c72defe1457389) (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) right) alt-right) 2))
      (body))
    (rule parser-pack-fact-5199db39c25af0b7ad46d0532ff7864d5b31e089b8796fb1ea3a7918000b9550
      (head (compile-pack-source-occurrence (pp-inline-label 2cc0f8250f8e6b1fd828d78ab2969901d5aac297e060c114b5c72defe1457389) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) alt-right) 3))
      (body))
    (rule parser-pack-fact-c9d001f8827e4e4b6491c126ebab108b6a62f307610f4fe092d442ba9189f502
      (head (compile-pack-source-occurrence (pp-inline-label 2cc0f8250f8e6b1fd828d78ab2969901d5aac297e060c114b5c72defe1457389) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) alt-right) 4))
      (body))
    (rule parser-pack-fact-ae2983a31ff7da24aa297df80d45cef53f70bf2a8987198370c8ebfb03ea75bd
      (head (compile-pack-source-occurrence (pp-inline-label 2cc0f8250f8e6b1fd828d78ab2969901d5aac297e060c114b5c72defe1457389) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) alt-right) 5))
      (body))
    (rule parser-pack-fact-407fe433ec1affa04afb5b1b83b44ac709900dc4d6ad551c26086a82f31ea566
      (head (compile-pack-source-occurrence (pp-inline-label 2cc0f8250f8e6b1fd828d78ab2969901d5aac297e060c114b5c72defe1457389) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) alt-right) 6))
      (body))
    (rule parser-pack-fact-0205142f9d7d94978ef23f70f9b0ce258cb097659624b247fdb3d68ca8dd81e4
      (head (compile-pack-source-occurrence (pp-inline-label 2cc0f8250f8e6b1fd828d78ab2969901d5aac297e060c114b5c72defe1457389) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) alt-right) 7))
      (body))
    (rule parser-pack-fact-0a4f30f2980751569b4c3371e4e118021b402ae9dcd2c08761558acade18b475
      (head (compile-pack-source-occurrence (pp-inline-label 2cc0f8250f8e6b1fd828d78ab2969901d5aac297e060c114b5c72defe1457389) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) right) ref) 8))
      (body))
    (rule parser-pack-fact-9a6b84a5507d7007ee846f19b8baed56bc4bf4e9703315787a62cd6527fb0b13
      (head (compile-pack-source-occurrence (pp-inline-label 2df4c611cf78d2d0996bcd0f2f190cf4fc80e0779141ac2e95e1c0ff5a6cef8c) (pp-label (pp-def kw-float-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-cfe8d58835d77e00ed5923de8f49ed761a46f9f1c9782774aeb3c6dd15236d22
      (head (compile-pack-source-occurrence (pp-inline-label 2df4c611cf78d2d0996bcd0f2f190cf4fc80e0779141ac2e95e1c0ff5a6cef8c) (pp-label (pp-sub (pp-def kw-float-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-5eb1fdf392beb62a46ee2ba3f7683129dfa1a26749c708ad0c3f16397d5b6b59
      (head (compile-pack-source-occurrence (pp-inline-label 2df4c611cf78d2d0996bcd0f2f190cf4fc80e0779141ac2e95e1c0ff5a6cef8c) (pp-label (pp-sub (pp-def kw-float-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-f62d5daf12111d17768f14e0a863ce73e1ba34f0b78aa721281ca7d25c00f339
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-def mm-production-statement_float) node) 0))
      (body))
    (rule parser-pack-fact-a72c6d29b916b37f154ab136c0103c1722e819f2872e3c3ed1d967718026672a
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-def mm-production-statement_float) body) seq) 1))
      (body))
    (rule parser-pack-fact-b88c07e2a50e0aee932a41044307fd3ea7882c35b73811640bc234911f15e7e3
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_float) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-819b627fd1c9ddd9bc53119f9f853f573229df815ddb5d80f6aa0e9729158a95
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) 3))
      (body))
    (rule parser-pack-fact-cbb5728bcf33ff5e6640f3489ce212978018cd694d99c2e60894c565061c3c30
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-3c75b92a07afc35495f1839732d8cdbe7aa2422846d654e3f17e0392d62428c4
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) seq) 5))
      (body))
    (rule parser-pack-fact-abd9d9ba41d0a4842faa0fa995896999919ddbe03b1258c931244a3f04d7de7d
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-7b1c10c9206e649ddbd9bc17a5b4a33af42c282dadc4ff28865a8b338c423d12
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) left) 7))
      (body))
    (rule parser-pack-fact-616473d06df4e3a3366b3e816061edc14af11f53a179ea048377eeb65316f831
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) left) ref) 8))
      (body))
    (rule parser-pack-fact-d003ddb02b390012639e991171fd3e07d01a2fb407ec0469b8c3697367c372d6
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) right) 9))
      (body))
    (rule parser-pack-fact-b2a14d28066c97c7701e18df119a0666d24c358ffff36c65f0eb6b5243b3131b
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) left) ref) 10))
      (body))
    (rule parser-pack-fact-9f9229dcb983a0cc4480ea1bd1755531557f4d9ed69a7f9e8ae83e4f83cff865
      (head (compile-pack-source-occurrence (pp-inline-label 331847d6fb4cf4eb93d91000cc953356bb6759cb79cba99a2b1dc57200c04949) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) right) eps) 11))
      (body))
    (rule parser-pack-fact-53834196acdd75c4cc6f35ee2d7c8a41c5e2fa16dfab01bafebc74e44f337c68
      (head (compile-pack-source-occurrence (pp-inline-label 385c4776f149bcb254a3de91559cbcf076a3041a605f2edb18aec469bd45fa6d) (pp-label (pp-sub (pp-def comment-chunk) left) plus-more) 0))
      (body))
    (rule parser-pack-fact-19b97cc5dab061f2cf6aa5f366c65c8dfbb36ae1e66721417efcc2dc56141b21
      (head (compile-pack-source-occurrence (pp-inline-label 385c4776f149bcb254a3de91559cbcf076a3041a605f2edb18aec469bd45fa6d) (pp-label (pp-sub (pp-sub (pp-def comment-chunk) left) body) ref) 1))
      (body))
    (rule parser-pack-fact-11f73358673a74d2b414e0e8bd6adff674e4fe6d0562628c28e9e6889bfa1355
      (head (compile-pack-source-occurrence (pp-inline-label 3a1eca614592aa23f1163a6b7ea4da1da6abe424c6e739d112865971d386fef5) (pp-label (pp-def mm-category-statement) alt-right) 0))
      (body))
    (rule parser-pack-fact-707da2107fb10d17493460db5c2b21281306cc5b58d267c3791cc091821acdc9
      (head (compile-pack-source-occurrence (pp-inline-label 3a1eca614592aa23f1163a6b7ea4da1da6abe424c6e739d112865971d386fef5) (pp-label (pp-sub (pp-def mm-category-statement) right) alt-right) 1))
      (body))
    (rule parser-pack-fact-c48d4826846f8f98d7937125f56b069f14662ec8070be5623a0ed77da5b46c8d
      (head (compile-pack-source-occurrence (pp-inline-label 3a1eca614592aa23f1163a6b7ea4da1da6abe424c6e739d112865971d386fef5) (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) right) alt-right) 2))
      (body))
    (rule parser-pack-fact-f5ed0c9b891075ef02bb2562b119d915909ffdc62bbfe7346f22aada31600c03
      (head (compile-pack-source-occurrence (pp-inline-label 3a1eca614592aa23f1163a6b7ea4da1da6abe424c6e739d112865971d386fef5) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) alt-right) 3))
      (body))
    (rule parser-pack-fact-65909f5886b48bf59d8ce1477448df82c47e05390c44653b8114bb4c288264fe
      (head (compile-pack-source-occurrence (pp-inline-label 3a1eca614592aa23f1163a6b7ea4da1da6abe424c6e739d112865971d386fef5) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) alt-right) 4))
      (body))
    (rule parser-pack-fact-c455ea102e8651c6a99c5d0230513bbc56fb1d0a3115e5bc299075b8240bc4d6
      (head (compile-pack-source-occurrence (pp-inline-label 3a1eca614592aa23f1163a6b7ea4da1da6abe424c6e739d112865971d386fef5) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) alt-right) 5))
      (body))
    (rule parser-pack-fact-d07a39fbd5dc1f996a048b4cd3f0efd988634b469f86bcb43a7a419088afdcdf
      (head (compile-pack-source-occurrence (pp-inline-label 3a1eca614592aa23f1163a6b7ea4da1da6abe424c6e739d112865971d386fef5) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) alt-left) 6))
      (body))
    (rule parser-pack-fact-0a8ae8dab700f18f35da9f0c9edd6d90d8c2460ebe74224c0f6edbe0549f9981
      (head (compile-pack-source-occurrence (pp-inline-label 3a1eca614592aa23f1163a6b7ea4da1da6abe424c6e739d112865971d386fef5) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) left) ref) 7))
      (body))
    (rule parser-pack-fact-fbadf191b96690ddeecfddf66bf02e4de7e0bf9306c3986cc7222f6fd2719e9d
      (head (compile-pack-source-occurrence (pp-inline-label 3d45336bbf7a3983ec1cc1b0245250037b40ec6712be02d5b0666c68f476cadd) (pp-label (pp-def kw-axiom) left) 0))
      (body))
    (rule parser-pack-fact-609058eb6fe52ea2876082aec3dacfcd93109c5fb502f3637c1d98457a05ed6c
      (head (compile-pack-source-occurrence (pp-inline-label 3d45336bbf7a3983ec1cc1b0245250037b40ec6712be02d5b0666c68f476cadd) (pp-label (pp-sub (pp-def kw-axiom) left) ref) 1))
      (body))
    (rule parser-pack-fact-6ab7c56967182dca4aae5ddf90a8a0f8dcf0433c0a63918932fc8ce931e0ea37
      (head (compile-pack-source-occurrence (pp-inline-label 3d45336bbf7a3983ec1cc1b0245250037b40ec6712be02d5b0666c68f476cadd) (pp-label (pp-sub (pp-def kw-axiom) right) ref) 2))
      (body))
    (rule parser-pack-fact-130fa3e24363f7e6fa0d595c3aeeb758fe4a99bd8fcdbf6cbaeec0b4b4bf5510
      (head (compile-pack-source-occurrence (pp-inline-label 3e7c4590cbe51656a034bbe71566b5473c543baa98f2e4f8cc206f7abb14f1df) (pp-label (pp-def proof-label-token-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-1caa5ff3acfd31e52ab0fe91a4dfea0c18051a6ae13be1458fe80cd26556a5e6
      (head (compile-pack-source-occurrence (pp-inline-label 3e7c4590cbe51656a034bbe71566b5473c543baa98f2e4f8cc206f7abb14f1df) (pp-label (pp-sub (pp-def proof-label-token-lexeme) left) node) 1))
      (body))
    (rule parser-pack-fact-e1b6753d8005a6f606adde99559395688d6f248709e34bb66557affce162b6ac
      (head (compile-pack-source-occurrence (pp-inline-label 3e7c4590cbe51656a034bbe71566b5473c543baa98f2e4f8cc206f7abb14f1df) (pp-label (pp-sub (pp-def proof-label-token-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-4786a1c41703caa6d0f979d2e9790dae6eaeec9f54ae551d75af9a6cf4b8ca0b
      (head (compile-pack-source-occurrence (pp-inline-label 3f697edd94e8958e836c1509bdefff4cc095f973e30f76aa34fb64d9a179458a) (pp-label (pp-def skip-unit) alt-right) 0))
      (body))
    (rule parser-pack-fact-16a288bd6a31e51457a94c22b38d9ddebd54b1644607331580c27eac56b9a26c
      (head (compile-pack-source-occurrence (pp-inline-label 3f697edd94e8958e836c1509bdefff4cc095f973e30f76aa34fb64d9a179458a) (pp-label (pp-sub (pp-def skip-unit) right) ref) 1))
      (body))
    (rule parser-pack-fact-ebd20b4a788934eefd1402832c729b94926068fc09037f9c4827813776d46038
      (head (compile-pack-source-occurrence (pp-inline-label 3f7e82d064a3e3177776eed310d8f559ef8b8ecfa46ed48191a3a33a1488ed8e) (pp-label (pp-def mm-production-proof_unknown_more) node) 0))
      (body))
    (rule parser-pack-fact-d640b1aec42f8821f39eb2eca24c9dd73ab99b5879ba8b229b740555a479eeaf
      (head (compile-pack-source-occurrence (pp-inline-label 3f7e82d064a3e3177776eed310d8f559ef8b8ecfa46ed48191a3a33a1488ed8e) (pp-label (pp-sub (pp-def mm-production-proof_unknown_more) body) right) 1))
      (body))
    (rule parser-pack-fact-2b7eac3d4e83d09f48a3a1092ae0cb53fab48c4665d09684a4f915411415d61b
      (head (compile-pack-source-occurrence (pp-inline-label 3f7e82d064a3e3177776eed310d8f559ef8b8ecfa46ed48191a3a33a1488ed8e) (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-971d951a3e9cfbc1fad21320e3e50684c16b5d3d7991b24ba690a609dd008cd8
      (head (compile-pack-source-occurrence (pp-inline-label 3f7e82d064a3e3177776eed310d8f559ef8b8ecfa46ed48191a3a33a1488ed8e) (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) left) 3))
      (body))
    (rule parser-pack-fact-1c6bd85a0173ce3a31dbb6b5e603067d67916a019a9730afd01bd9ca8e28d065
      (head (compile-pack-source-occurrence (pp-inline-label 3f7e82d064a3e3177776eed310d8f559ef8b8ecfa46ed48191a3a33a1488ed8e) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-adabc3b59d39e2b92fdc7757fdb7da3f64cf4d511a98b8be7450f00e87aed7ce
      (head (compile-pack-source-occurrence (pp-inline-label 3f7e82d064a3e3177776eed310d8f559ef8b8ecfa46ed48191a3a33a1488ed8e) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) right) eps) 5))
      (body))
    (rule parser-pack-fact-9d0613602e5c013e4202343e7bc9e7ec5a8fabb69b8b78471cf359410fd91219
      (head (compile-pack-source-occurrence (pp-inline-label 40a8fc6c5f1bd1b4fff29fc01d36c0c02b9a0caaea5cca96f461fd2116fc45ed) (pp-label (pp-def mm-production-outer_database_empty) node) 0))
      (body))
    (rule parser-pack-fact-c87fecd816f01e5169fbf1df9ff0b9dda30d283bc328ca0acb586f031eeafe11
      (head (compile-pack-source-occurrence (pp-inline-label 40a8fc6c5f1bd1b4fff29fc01d36c0c02b9a0caaea5cca96f461fd2116fc45ed) (pp-label (pp-sub (pp-def mm-production-outer_database_empty) body) eps) 1))
      (body))
    (rule parser-pack-fact-51e98c27d8fc89352bc147910aa1483fa16a0193bb1e2894f9dce58c72c5ded4
      (head (compile-pack-source-occurrence (pp-inline-label 48b54cb65e282199132af41529c98c69655def088005db9c2e4c8735a7e00595) (pp-label (pp-def kw-disjoint) left) 0))
      (body))
    (rule parser-pack-fact-60cbb07c32fa7f12833d0f4a4693c2a3ac18e3bd932f051a03687aed38f2dee8
      (head (compile-pack-source-occurrence (pp-inline-label 48b54cb65e282199132af41529c98c69655def088005db9c2e4c8735a7e00595) (pp-label (pp-sub (pp-def kw-disjoint) left) ref) 1))
      (body))
    (rule parser-pack-fact-3321ac9050cfb2940c7ca2ab12ec2d0a9714c08c9e5e86473bcb1ea98fa105a9
      (head (compile-pack-source-occurrence (pp-inline-label 48b54cb65e282199132af41529c98c69655def088005db9c2e4c8735a7e00595) (pp-label (pp-sub (pp-def kw-disjoint) right) ref) 2))
      (body))
    (rule parser-pack-fact-37aebd920cd81a8af2ba77ec435a0d2f974d8181a6e60896a3ee9505a62a0546
      (head (compile-pack-source-occurrence (pp-inline-label 48d4c0b4f341ea7eb5eadaaaddae0591d42956a99fb0ff728dec3884d268458a) (pp-label (pp-def mm-production-proof_unknown_one) node) 0))
      (body))
    (rule parser-pack-fact-be2ad557b8f034d916e910d3cc2d892eb0aa737ddfa9f875a2dbfafba29ff912
      (head (compile-pack-source-occurrence (pp-inline-label 48d4c0b4f341ea7eb5eadaaaddae0591d42956a99fb0ff728dec3884d268458a) (pp-label (pp-sub (pp-def mm-production-proof_unknown_one) body) right) 1))
      (body))
    (rule parser-pack-fact-f9a7c2a6c49235913f4984c6dbd84a00f09f0728e57e186a12f6a207b1481742
      (head (compile-pack-source-occurrence (pp-inline-label 48d4c0b4f341ea7eb5eadaaaddae0591d42956a99fb0ff728dec3884d268458a) (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_unknown_one) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-76f791da989bb7b50b6350e8d120559e6b56c36e24d3546d57df30b17a732a5c
      (head (compile-pack-source-occurrence (pp-inline-label 48d4c0b4f341ea7eb5eadaaaddae0591d42956a99fb0ff728dec3884d268458a) (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_unknown_one) body) right) eps) 3))
      (body))
    (rule parser-pack-fact-292b4df7f21bed34f195a50b9d8878c1bb258af535346a6e6a059f6c925db888
      (head (compile-pack-source-occurrence (pp-inline-label 497a1c9a08a0db1c0757eb9f8f569e1b1ca2d5d602ae34749ff71c901d9df29d) (pp-label (pp-def kw-include-open-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-41dd49ec00e1ad94bf9430c38af3fe4a98a7f39ed631a6176bb5c777002d2371
      (head (compile-pack-source-occurrence (pp-inline-label 497a1c9a08a0db1c0757eb9f8f569e1b1ca2d5d602ae34749ff71c901d9df29d) (pp-label (pp-sub (pp-def kw-include-open-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-e41d76fb488d5aecdce7408fb8e0b37123a226fbe57cf90d8631d0e4c308b6cb
      (head (compile-pack-source-occurrence (pp-inline-label 497a1c9a08a0db1c0757eb9f8f569e1b1ca2d5d602ae34749ff71c901d9df29d) (pp-label (pp-sub (pp-def kw-include-open-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-fb10f4273fd40271dcbf62a971f67485de916a7fb8025651dd6c84d5b7400126
      (head (compile-pack-source-occurrence (pp-inline-label 4a0b76ed2ebcd72851b2dcc8f3076580e4714c77cb0cacc1307dc85dbebee383) (pp-label (pp-def kw-end) left) 0))
      (body))
    (rule parser-pack-fact-c45d15d99288784f54b9135ca2a339ae9c6aaaa3a9ec1dc585b9bb5115477089
      (head (compile-pack-source-occurrence (pp-inline-label 4a0b76ed2ebcd72851b2dcc8f3076580e4714c77cb0cacc1307dc85dbebee383) (pp-label (pp-sub (pp-def kw-end) left) ref) 1))
      (body))
    (rule parser-pack-fact-c1d2427c9e2cfc2767af55a91ad6c56df35ab70bd1907bd25ea36a92729e602e
      (head (compile-pack-source-occurrence (pp-inline-label 4a0b76ed2ebcd72851b2dcc8f3076580e4714c77cb0cacc1307dc85dbebee383) (pp-label (pp-sub (pp-def kw-end) right) ref) 2))
      (body))
    (rule parser-pack-fact-2a7ef32c5c4d113542ce0ad20e961969701c12f41ae42363fab2b29f01d66131
      (head (compile-pack-source-occurrence (pp-inline-label 4a81611cc535df379159b9f6bd9fe66fc54c6a851d6634dc61025cbf9b6cbd03) (pp-label (pp-def mm-production-proof_more) node) 0))
      (body))
    (rule parser-pack-fact-1d9911d73695c16934c3b24903c3f752b83dbb20dd44511bff0e06bb71cc5212
      (head (compile-pack-source-occurrence (pp-inline-label 4a81611cc535df379159b9f6bd9fe66fc54c6a851d6634dc61025cbf9b6cbd03) (pp-label (pp-sub (pp-def mm-production-proof_more) body) seq) 1))
      (body))
    (rule parser-pack-fact-dfaf52e4a7b71a520621f482b527efe4c72685513f6892c64f2cf5a4678b3f8c
      (head (compile-pack-source-occurrence (pp-inline-label 4a81611cc535df379159b9f6bd9fe66fc54c6a851d6634dc61025cbf9b6cbd03) (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_more) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-383347485ff0288cf6fe0ead6818a5d3258242cbe7e57866ec1f4c7cdb50fb2a
      (head (compile-pack-source-occurrence (pp-inline-label 4a81611cc535df379159b9f6bd9fe66fc54c6a851d6634dc61025cbf9b6cbd03) (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) left) 3))
      (body))
    (rule parser-pack-fact-78eaf1cb8ba1936fc6095fc7682c044e0a0c42138a32d29d3091ecb8a4b50cac
      (head (compile-pack-source-occurrence (pp-inline-label 4a81611cc535df379159b9f6bd9fe66fc54c6a851d6634dc61025cbf9b6cbd03) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-9670d9811042fb2d229fd3d3862cc6cf726e095d8fea235c7d315b0af5ef9d35
      (head (compile-pack-source-occurrence (pp-inline-label 4a81611cc535df379159b9f6bd9fe66fc54c6a851d6634dc61025cbf9b6cbd03) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) right) eps) 5))
      (body))
    (rule parser-pack-fact-e34cd91eb75a5b92d0aebe46f3c95eb8f22ceaa7c6d473fc6d109100bf16f349
      (head (compile-pack-source-occurrence (pp-inline-label 4cd14fad0bd4a6b00efa83ca124edce8209069323c54e3d7db34aba99c7b17df) (pp-label (pp-def proof-label-token) left) 0))
      (body))
    (rule parser-pack-fact-7c6887ec3eba0fa1bfbc5d26e7d16d1d24d345514593ec1426c4f02e4622dbc0
      (head (compile-pack-source-occurrence (pp-inline-label 4cd14fad0bd4a6b00efa83ca124edce8209069323c54e3d7db34aba99c7b17df) (pp-label (pp-sub (pp-def proof-label-token) left) ref) 1))
      (body))
    (rule parser-pack-fact-b09c711fa502bebd4c19dc449ac679cd22c026330f4ba974edfe195acbdd6f79
      (head (compile-pack-source-occurrence (pp-inline-label 4cd14fad0bd4a6b00efa83ca124edce8209069323c54e3d7db34aba99c7b17df) (pp-label (pp-sub (pp-def proof-label-token) right) ref) 2))
      (body))
    (rule parser-pack-fact-7454c4330e8d52a856a7170ee41ca4717fa39fe164f8c0acabf20f6669f2a066
      (head (compile-pack-source-occurrence (pp-inline-label 4d15101a28388cf0d9919a00ef50bff04f843139fb71291731d50bdf4376b093) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) star-cons) 0))
      (body))
    (rule parser-pack-fact-88c8c381bb4bc85f90c7a9df8625cbe097333dbb5df3117728582ebc689cac9b
      (head (compile-pack-source-occurrence (pp-inline-label 4d15101a28388cf0d9919a00ef50bff04f843139fb71291731d50bdf4376b093) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) seq) 1))
      (body))
    (rule parser-pack-fact-36cc32474e0bb6a5ffc3a444422af893995e8d95fa225b1f4e4f9ec0d25d26bb
      (head (compile-pack-source-occurrence (pp-inline-label 4d15101a28388cf0d9919a00ef50bff04f843139fb71291731d50bdf4376b093) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-74ba47897cda6851a069986cd7954da1e4b5929074740a19d6b3328727253c42
      (head (compile-pack-source-occurrence (pp-inline-label 4d15101a28388cf0d9919a00ef50bff04f843139fb71291731d50bdf4376b093) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) right) ref) 3))
      (body))
    (rule parser-pack-fact-84538db6793dda6c734b54a8f9de493ad688310019d9b4a9ad15bad6870b2e0e
      (head (compile-pack-source-occurrence (pp-inline-label 4fdfe6fe05a42c9eaa4ca7e4b5af6fe8816ef838a4bc463fc0abca44c8d9d932) (pp-label (pp-def mm-production-statement_var) node) 0))
      (body))
    (rule parser-pack-fact-83c040bd37273834a732978154f4238c57f622d8c5b3f4ebcedac4fd7a05da18
      (head (compile-pack-source-occurrence (pp-inline-label 4fdfe6fe05a42c9eaa4ca7e4b5af6fe8816ef838a4bc463fc0abca44c8d9d932) (pp-label (pp-sub (pp-def mm-production-statement_var) body) right) 1))
      (body))
    (rule parser-pack-fact-368afb131d3b912e1ba4cb8ad4e0a0ee6321ebcb3fc8d4b765ab92db67f2f076
      (head (compile-pack-source-occurrence (pp-inline-label 4fdfe6fe05a42c9eaa4ca7e4b5af6fe8816ef838a4bc463fc0abca44c8d9d932) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_var) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-cae407b24e9161885e758dd94bd8b8a3918401033c8ec17af12c74ce8a644c0a
      (head (compile-pack-source-occurrence (pp-inline-label 4fdfe6fe05a42c9eaa4ca7e4b5af6fe8816ef838a4bc463fc0abca44c8d9d932) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) left) 3))
      (body))
    (rule parser-pack-fact-0a08a5c3c90c721634869587b3dcfc9b865f436adb26d601904b9f52226943d1
      (head (compile-pack-source-occurrence (pp-inline-label 4fdfe6fe05a42c9eaa4ca7e4b5af6fe8816ef838a4bc463fc0abca44c8d9d932) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-9f63f1801685918ccb6406407259bcf894951c6e09663686ce952c59d33dd082
      (head (compile-pack-source-occurrence (pp-inline-label 4fdfe6fe05a42c9eaa4ca7e4b5af6fe8816ef838a4bc463fc0abca44c8d9d932) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) right) 5))
      (body))
    (rule parser-pack-fact-bb732cedcee4ebda4ef99c9a523bfdea6e87811017f4e0697a42fb97b97332b1
      (head (compile-pack-source-occurrence (pp-inline-label 4fdfe6fe05a42c9eaa4ca7e4b5af6fe8816ef838a4bc463fc0abca44c8d9d932) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-699c875ef7cbf01b32f694366839d93c91743a1982b563dc92334fd7346c903c
      (head (compile-pack-source-occurrence (pp-inline-label 4fdfe6fe05a42c9eaa4ca7e4b5af6fe8816ef838a4bc463fc0abca44c8d9d932) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) right) eps) 7))
      (body))
    (rule parser-pack-fact-42cd6d013ec29f4f16a908443488d2b7ba902a6116bfac8bc22f043e53834ff8
      (head (compile-pack-source-occurrence (pp-inline-label 50363561cbc74f751bde3f25c0974103abe66616641c848c6d4dd099e2498475) (pp-label (pp-def skip) star-empty) 0))
      (body))
    (rule parser-pack-fact-53d2c07f55c088251b1ce11c0b45cddc90e1b09e6e917c36c8b483fdafbbd2eb
      (head (compile-pack-source-occurrence (pp-inline-label 50adf1502184a6191d08c16496e705e952d4dc435ba86c8505d96148b91a776e) (pp-label (pp-def kw-block-open-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-b098759e4005a571eb03b3b3c80a2ff4e1553db8b2d5f1248a5e8be36713c812
      (head (compile-pack-source-occurrence (pp-inline-label 50adf1502184a6191d08c16496e705e952d4dc435ba86c8505d96148b91a776e) (pp-label (pp-sub (pp-def kw-block-open-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-b021b106042a57d972c499c47170a7ff2b5a258a2e24203894afb131a9311b45
      (head (compile-pack-source-occurrence (pp-inline-label 50adf1502184a6191d08c16496e705e952d4dc435ba86c8505d96148b91a776e) (pp-label (pp-sub (pp-def kw-block-open-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-86ba49ce6720064e0387f0a30779f366199910aeb7bc81cdd2ffed37707e7bfa
      (head (compile-pack-source-occurrence (pp-inline-label 50d9e195dfc653057bda9b02302219f80452f9a256576b413a14c7f719c1e910) (pp-label (pp-def skip) star-cons) 0))
      (body))
    (rule parser-pack-fact-be5a1b2a79675712a84cbde3b3fba02feee4918ca4cff5d162ec11222141cc2a
      (head (compile-pack-source-occurrence (pp-inline-label 50d9e195dfc653057bda9b02302219f80452f9a256576b413a14c7f719c1e910) (pp-label (pp-sub (pp-def skip) body) ref) 1))
      (body))
    (rule parser-pack-fact-fab023ff12921e5a17b64b119154355094f51948fbbee66ca25992d66b044458
      (head (compile-pack-source-occurrence (pp-inline-label 513391a1b0c2389781214e3a2e14f0ae9ba39116033156abb9a7c280b3295398) (pp-label (pp-def mm-category-statement) alt-right) 0))
      (body))
    (rule parser-pack-fact-a22991fa79257481c69786a2ffbae9973c099fd122ace13fc5d2f943220445ee
      (head (compile-pack-source-occurrence (pp-inline-label 513391a1b0c2389781214e3a2e14f0ae9ba39116033156abb9a7c280b3295398) (pp-label (pp-sub (pp-def mm-category-statement) right) alt-left) 1))
      (body))
    (rule parser-pack-fact-0c88f05ea72f52e4bf71f46e25475b36caa322fc36a3b8e53150400c522f2792
      (head (compile-pack-source-occurrence (pp-inline-label 513391a1b0c2389781214e3a2e14f0ae9ba39116033156abb9a7c280b3295398) (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) left) ref) 2))
      (body))
    (rule parser-pack-fact-9fa30e94f60e0b8fb732067686dce3e6506b2c82b8cf4e504b57edcbb9fba9ad
      (head (compile-pack-source-occurrence (pp-inline-label 53ea97c810f512a1180465264941005c56558d23f95bfb15eb19746f3c79b0a1) (pp-label (pp-def kw-essential-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-f41b9018f8f9e0daf4ba325e4a663ddff678a73b581038884a017ee5b34a6d18
      (head (compile-pack-source-occurrence (pp-inline-label 53ea97c810f512a1180465264941005c56558d23f95bfb15eb19746f3c79b0a1) (pp-label (pp-sub (pp-def kw-essential-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-9cdd956641991e2a48eb8705b7b7ea70f8245f3b7cc97bb047e3bc240113aa0d
      (head (compile-pack-source-occurrence (pp-inline-label 53ea97c810f512a1180465264941005c56558d23f95bfb15eb19746f3c79b0a1) (pp-label (pp-sub (pp-def kw-essential-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-9926755c8bb6f07971d96adddcbf29d63be7c8dacd589c31c6ff0e26985983f9
      (head (compile-pack-source-occurrence (pp-inline-label 5492d695f002186d7bfc8361115fb6d978cced046418f1e54674dcdd86fffa21) (pp-label (pp-def mm-category-symbol_list) alt-right) 0))
      (body))
    (rule parser-pack-fact-4642f89a16b99b9b501afba8ad0ecab2638c346fb98cbd7df63a05a283566087
      (head (compile-pack-source-occurrence (pp-inline-label 5492d695f002186d7bfc8361115fb6d978cced046418f1e54674dcdd86fffa21) (pp-label (pp-sub (pp-def mm-category-symbol_list) right) ref) 1))
      (body))
    (rule parser-pack-fact-e51c0c9a6462fd9c530ac773935e454e2b5dcb313a5f906b3f66b3252568b4e2
      (head (compile-pack-source-occurrence (pp-inline-label 585ea1c332f0a0b4e708a66b89b60f197a0ea1ca8764cbd51e06efa4b7a19a0b) (pp-label (pp-def mm-category-outer_database) alt-left) 0))
      (body))
    (rule parser-pack-fact-6a1e50e18b24cf6f07264b35eafe838c9d06907060d699f4ad5d309b5bc99150
      (head (compile-pack-source-occurrence (pp-inline-label 585ea1c332f0a0b4e708a66b89b60f197a0ea1ca8764cbd51e06efa4b7a19a0b) (pp-label (pp-sub (pp-def mm-category-outer_database) left) ref) 1))
      (body))
    (rule parser-pack-fact-3c231926eb04c8d4220f958fd97f4c2ec83968307f4aa78af4707b935cb41a06
      (head (compile-pack-source-occurrence (pp-inline-label 58eabe6d56cfe6aabb3ded3e660a28e29cf9a5c4f843bd7e66b8bd5c8cb83592) (pp-label (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) plus-one) 0))
      (body))
    (rule parser-pack-fact-f6e1da706b0b3a140319babf6b07acd9c99954f23ac421d4a8cef8dc1d19472d
      (head (compile-pack-source-occurrence (pp-inline-label 58eabe6d56cfe6aabb3ded3e660a28e29cf9a5c4f843bd7e66b8bd5c8cb83592) (pp-label (pp-sub (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) body) class) 1))
      (body))
    (rule parser-pack-fact-567a48082950b996a5b4ad140387cb302e2db535ff6d98e158d24800761feafa
      (head (compile-pack-source-occurrence (pp-inline-label 5a4b7032651d1151858f8ff1377a2d66ccc8a9b97287c589d618e20af912a4bb) (pp-label (pp-def left-paren-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-5e46fc5b8c2d11b73b22a292d53351b372b1bf3be72c4d4105c964ed48782d54
      (head (compile-pack-source-occurrence (pp-inline-label 5a4b7032651d1151858f8ff1377a2d66ccc8a9b97287c589d618e20af912a4bb) (pp-label (pp-sub (pp-def left-paren-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-c9cc37fd9bad615fe453f71e9f44d55ad9347e3a5ae83922434cc51bbd1f8dc1
      (head (compile-pack-source-occurrence (pp-inline-label 5a4b7032651d1151858f8ff1377a2d66ccc8a9b97287c589d618e20af912a4bb) (pp-label (pp-sub (pp-def left-paren-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-66880d85a14f9dbbb5b05b4c44989cdfdb10098bf0eedf8758200230f4f8596c
      (head (compile-pack-source-occurrence (pp-inline-label 5a74ae060295ad8f925c8b441bad0b4c44a39e7b686aef2e8f8f9e421d3aaf0d) (pp-label (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) plus-more) 0))
      (body))
    (rule parser-pack-fact-2a6040412a72f7b28a5edb31b2e3ca531786055d9bb091aaa037cac217db5174
      (head (compile-pack-source-occurrence (pp-inline-label 5a74ae060295ad8f925c8b441bad0b4c44a39e7b686aef2e8f8f9e421d3aaf0d) (pp-label (pp-sub (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) body) class) 1))
      (body))
    (rule parser-pack-fact-fe83fc84a0489c83b501a4c2cab96a57ed48d5cb9b144b1e03f27ea42990b14a
      (head (compile-pack-source-occurrence (pp-inline-label 5c87514f7ba36c1ef24291eab905cb355f9b43ae60069672e682b6bd9cbd338e) (pp-label (pp-def mm-production-statement_const) node) 0))
      (body))
    (rule parser-pack-fact-5b0373fd4d341a46185bc2b234e44793f7db03e3a1cb1f0e845e1e49bea81d78
      (head (compile-pack-source-occurrence (pp-inline-label 5c87514f7ba36c1ef24291eab905cb355f9b43ae60069672e682b6bd9cbd338e) (pp-label (pp-sub (pp-def mm-production-statement_const) body) right) 1))
      (body))
    (rule parser-pack-fact-7d1132568a40f068a0417f43fe737dc7c6779f21be29654453662a7c13056aa2
      (head (compile-pack-source-occurrence (pp-inline-label 5c87514f7ba36c1ef24291eab905cb355f9b43ae60069672e682b6bd9cbd338e) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_const) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-3a65c3cabc8051d79def99156a1d006cd43771a86551f43d7d0e6e95d97e236b
      (head (compile-pack-source-occurrence (pp-inline-label 5c87514f7ba36c1ef24291eab905cb355f9b43ae60069672e682b6bd9cbd338e) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) left) 3))
      (body))
    (rule parser-pack-fact-5784f8e729bd5e144dcf8b0d153d22c44c39443ebbe2633df4f702720af4fd01
      (head (compile-pack-source-occurrence (pp-inline-label 5c87514f7ba36c1ef24291eab905cb355f9b43ae60069672e682b6bd9cbd338e) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-be9ae978fe3c7697849ba9b13141002438084a2ea9c5ac411060faf3e0420c4d
      (head (compile-pack-source-occurrence (pp-inline-label 5c87514f7ba36c1ef24291eab905cb355f9b43ae60069672e682b6bd9cbd338e) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) right) 5))
      (body))
    (rule parser-pack-fact-85f70b63340e45b17ef71509a2718344314f34daa5bcbcf28be49fc8b45ad5ac
      (head (compile-pack-source-occurrence (pp-inline-label 5c87514f7ba36c1ef24291eab905cb355f9b43ae60069672e682b6bd9cbd338e) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-5d6938de2fcbf6882c416da19a6c2e877290524fd4c24ac3786434f33054bf04
      (head (compile-pack-source-occurrence (pp-inline-label 5c87514f7ba36c1ef24291eab905cb355f9b43ae60069672e682b6bd9cbd338e) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) right) eps) 7))
      (body))
    (rule parser-pack-fact-732c75b8c83b68de603371f44b965641b0c9b8a5b629cc5acb6dff81656bf28a
      (head (compile-pack-source-occurrence (pp-inline-label 5ca9f417be471a2f03da5924375e9be37507d88585351c10c951991c04a31bc3) (pp-label (pp-sub (pp-def comment-chunk) left) plus-one) 0))
      (body))
    (rule parser-pack-fact-1744c2e862145063a168823cbddb5ca8c4688bdbee724cf1cd4e478a214531b5
      (head (compile-pack-source-occurrence (pp-inline-label 5ca9f417be471a2f03da5924375e9be37507d88585351c10c951991c04a31bc3) (pp-label (pp-sub (pp-sub (pp-def comment-chunk) left) body) ref) 1))
      (body))
    (rule parser-pack-fact-77bcde4b5bfaa358780bf86a82e3e621b20d3e73ef07ecea1d7b8958158fceef
      (head (compile-pack-source-occurrence (pp-inline-label 5e08f679362e22c908cd3c78cc6efcd2ed8dfb2ee77fb915a8a3a46e8611ceb7) (pp-label (pp-sub (pp-sub (pp-def comment-chunk) right) left) star-empty) 0))
      (body))
    (rule parser-pack-fact-6f825791f8a40eb8640beb474fa60677db708fed10b26c626964a05009ce2e3f
      (head (compile-pack-source-occurrence (pp-inline-label 5ea94606b9f2b33d374458ac9ca9831737a41c601fae40427751e18df05993a0) (pp-label (pp-def right-paren) left) 0))
      (body))
    (rule parser-pack-fact-7e601b1268dc8fd5172e9655b2ddeb9c70cf852aa22ce1d47c4c4243251582f2
      (head (compile-pack-source-occurrence (pp-inline-label 5ea94606b9f2b33d374458ac9ca9831737a41c601fae40427751e18df05993a0) (pp-label (pp-sub (pp-def right-paren) left) ref) 1))
      (body))
    (rule parser-pack-fact-20d8c838d34fa47d4d2c22bccecf53fad2a071332f3c848cf36d58c5f0ce212b
      (head (compile-pack-source-occurrence (pp-inline-label 5ea94606b9f2b33d374458ac9ca9831737a41c601fae40427751e18df05993a0) (pp-label (pp-sub (pp-def right-paren) right) ref) 2))
      (body))
    (rule parser-pack-fact-e70ed82e2d319c4b14738f213abc9519aa77f9ab56c9aedbbbe967498b352ee7
      (head (compile-pack-source-occurrence (pp-inline-label 5ff6283601680537577d2177ce846cbebebe87d3adf0aff49fd22b45c4b5a354) (pp-label (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) plus-one) 0))
      (body))
    (rule parser-pack-fact-247d322863bffafc840ac1d997f9085ab9c3ce74945887fcf9c57d6863404bb8
      (head (compile-pack-source-occurrence (pp-inline-label 5ff6283601680537577d2177ce846cbebebe87d3adf0aff49fd22b45c4b5a354) (pp-label (pp-sub (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) body) class) 1))
      (body))
    (rule parser-pack-fact-b15466802212b4af488b1185e1f10af371fe0b6d1a788d9f98608662df655fc9
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-def mm-production-statement_theorem_compressed) node) 0))
      (body))
    (rule parser-pack-fact-b70f0d5552ba6b2eada2bba8ab1dcfb29efccd09bfffc684cb8395ff56388e2c
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-def mm-production-statement_theorem_compressed) body) seq) 1))
      (body))
    (rule parser-pack-fact-4fc617c89673e7bb2e00525e52fb22196a9bad14bf5e9e9f4e1964a22ba222c7
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-c41aa00351def21ffe7c632ab100581bb33091513d3d24e593ddc463a71c742d
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) 3))
      (body))
    (rule parser-pack-fact-30edc43729e93820719b9e673b29f1051489c66956c53d4f4d19137e37f7d63a
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-2d2c00397463fdf7ee630bcac7cede3644f96dc47ef87dc35dc6978ba8ecf64c
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) seq) 5))
      (body))
    (rule parser-pack-fact-6137ed4630372f570a16872f7244405a47ca79d618e85ccdc7bb6658959f3456
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-5dfb8edd7ab317e36d40acea757e11c243f5f29ad3f302450ff81789668558ba
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) 7))
      (body))
    (rule parser-pack-fact-c231feab7d87e2692a6b0dc25029e6321d35de9fedc58237b8b3d12fd6ce0bde
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) left) ref) 8))
      (body))
    (rule parser-pack-fact-0eb5e267eb8a5dc345d021f6c8558dbdde6abf08ed8dbcb376f9f8f3b5da43bd
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) 9))
      (body))
    (rule parser-pack-fact-06ed1d32bf8bcbfbcdcd885b8259d7e2b15cabc7a1d778254afd64ea49043467
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) left) ref) 10))
      (body))
    (rule parser-pack-fact-bd9f651e1804f6a1e0f86cb8583cda063fe6dc8d64deaa8074ec992345b952cc
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) seq) 11))
      (body))
    (rule parser-pack-fact-c9a57f2bd6341fe9b5d7582efd299e6146b515d9ab499621f5a7341f5c585f07
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) left) ref) 12))
      (body))
    (rule parser-pack-fact-f6827e464308e322a9b318aa0f6e2ffe824e5129b2c25e439eaa8c2f976a552b
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) 13))
      (body))
    (rule parser-pack-fact-733418095cf59fb0acc5590de6aceaad03ec57d3c5f329541335e716a50ac4ce
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) left) ref) 14))
      (body))
    (rule parser-pack-fact-92548e9d1201e291663d8aa76fb6675fec5cbb37c8472f4628a97794f90566f8
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) left) 15))
      (body))
    (rule parser-pack-fact-d961c2bccecbeda93587e469636e8961d000731e6248885d0dd0dfb78c23e853
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) left) ref) 16))
      (body))
    (rule parser-pack-fact-68cc841bcc570273ab3fa9afdeeb10f11b941f2c19dd516573a5a611f97ce7e1
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) right) 17))
      (body))
    (rule parser-pack-fact-bda5f0ef5c20fb66a4850248bfd0e0cdb0ba5a2618194581abefb6a7bf201da7
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) left) ref) 18))
      (body))
    (rule parser-pack-fact-5a2514b1b79b1561b9fb6ff7ad536d8df1d98e7d8d65393c3af0aff490f07c39
      (head (compile-pack-source-occurrence (pp-inline-label 62baec82257066d199ff7965c298a1382003a803cb047300d68001bdebf29627) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) right) eps) 19))
      (body))
    (rule parser-pack-fact-180437d09aab198307b011bab17514947b9b8e9adc4b12350aa48b81dfa3d87d
      (head (compile-pack-source-occurrence (pp-inline-label 64221477dadfb3c84f2f3a038a40a618e08f2a2ca55d1ddfe2be2b054f50b326) (pp-label (pp-def mm-category-proof_list) alt-left) 0))
      (body))
    (rule parser-pack-fact-27811d059fbbebad34a6898e0efefb904d26504970a1a7b8d05ed5821ce61a0c
      (head (compile-pack-source-occurrence (pp-inline-label 64221477dadfb3c84f2f3a038a40a618e08f2a2ca55d1ddfe2be2b054f50b326) (pp-label (pp-sub (pp-def mm-category-proof_list) left) ref) 1))
      (body))
    (rule parser-pack-fact-98a82290a95d1fb83a2c13b019c2eeaaa20ede6370f154eafac82f5fb530d133
      (head (compile-pack-source-occurrence (pp-inline-label 67e650bd9426f86d6f5b9b3f1e449e055b73d9bf09fdae6f235bcd9afa3b8d61) (pp-label (pp-def mm-production-proof_one) node) 0))
      (body))
    (rule parser-pack-fact-b86b3912eae23905c83941be37a76c83b774a4a6f231d19f8ef7e2f09db50aec
      (head (compile-pack-source-occurrence (pp-inline-label 67e650bd9426f86d6f5b9b3f1e449e055b73d9bf09fdae6f235bcd9afa3b8d61) (pp-label (pp-sub (pp-def mm-production-proof_one) body) left) 1))
      (body))
    (rule parser-pack-fact-3a154eb664d72349082dbf79f6466eddb152f4ba69fc251ff9ccbaeef5d94a45
      (head (compile-pack-source-occurrence (pp-inline-label 67e650bd9426f86d6f5b9b3f1e449e055b73d9bf09fdae6f235bcd9afa3b8d61) (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_one) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-f09f8ec367800a5d5c216d3b2cfab2d45420b175f48c29187374c66618ac286f
      (head (compile-pack-source-occurrence (pp-inline-label 67e650bd9426f86d6f5b9b3f1e449e055b73d9bf09fdae6f235bcd9afa3b8d61) (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_one) body) right) eps) 3))
      (body))
    (rule parser-pack-fact-40aaacaf00a52ea816cbd560c62a60e90f57dc1f38e5b6c24d4839c23bdbfca0
      (head (compile-pack-source-occurrence (pp-inline-label 6a66bb9b972338d1a483bbbe95c837716821346fc8d4e8a9e6fa8e7f62dbeaf9) (pp-label (pp-def kw-include-close-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-43cec7ec53eb81259d3726017d1143f4d9e26aee07f1ebf863c2ffc383c0715d
      (head (compile-pack-source-occurrence (pp-inline-label 6a66bb9b972338d1a483bbbe95c837716821346fc8d4e8a9e6fa8e7f62dbeaf9) (pp-label (pp-sub (pp-def kw-include-close-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-617461bb35b84a3800da1ffc120f80355b5895cb6927db0f477d5277c33b6e04
      (head (compile-pack-source-occurrence (pp-inline-label 6a66bb9b972338d1a483bbbe95c837716821346fc8d4e8a9e6fa8e7f62dbeaf9) (pp-label (pp-sub (pp-def kw-include-close-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-7f511f6f10ec8c29c1255a54cfc3cac7f004b99eb4555ec0b72557f039d37c7d
      (head (compile-pack-source-occurrence (pp-inline-label 6c45f3a69143a6c02cc760a06e85d0007b69323bd0ca59997e17ea09e074219c) (pp-label (pp-def mm-category-proof_list) alt-right) 0))
      (body))
    (rule parser-pack-fact-2aa0661d9837fd8dd5558bf98a0dfb4b5e2279a91443371778f2d6238331e61f
      (head (compile-pack-source-occurrence (pp-inline-label 6c45f3a69143a6c02cc760a06e85d0007b69323bd0ca59997e17ea09e074219c) (pp-label (pp-sub (pp-def mm-category-proof_list) right) alt-left) 1))
      (body))
    (rule parser-pack-fact-bb5c2c1f9961b9cfd28706f089576777568fa554b340bf2abf9ebc766ce68a97
      (head (compile-pack-source-occurrence (pp-inline-label 6c45f3a69143a6c02cc760a06e85d0007b69323bd0ca59997e17ea09e074219c) (pp-label (pp-sub (pp-sub (pp-def mm-category-proof_list) right) left) ref) 2))
      (body))
    (rule parser-pack-fact-54bf614e0361b63ba3b089a49851adb7d40c2ba935af2a8a81e75c6cc953baea
      (head (compile-pack-source-occurrence (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-label (pp-def mm-production-statement_essential) node) 0))
      (body))
    (rule parser-pack-fact-e5495bb84105378991b8c5eaf48255792984c978fca31aaa3ff9c71f68d40b24
      (head (compile-pack-source-occurrence (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-label (pp-sub (pp-def mm-production-statement_essential) body) seq) 1))
      (body))
    (rule parser-pack-fact-aa615ecf70e210500faf126ffbdd1d4e021720836f8f4bcde4ee64cdee48f7e3
      (head (compile-pack-source-occurrence (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-852fd38a828060dabe798360b8678a105fa2b84712b2037faf9c00d2d6b6c8c9
      (head (compile-pack-source-occurrence (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) 3))
      (body))
    (rule parser-pack-fact-0bef22ddc9f5ca8a610fcd7be306e5d684c6fb3bc82e309453e8c7c95025d18b
      (head (compile-pack-source-occurrence (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-4fd366da15f4980750f67b239f3e764e751e6a00a20f17b8e3cf430929cb4663
      (head (compile-pack-source-occurrence (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) left) 5))
      (body))
    (rule parser-pack-fact-ba81e876e34459313cc88e8e809375b9bf0369bcc07c933839281c80518082a2
      (head (compile-pack-source-occurrence (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-aa9072d1a54fcccfb87b15f30c241f912a73116fe80cf92f8ec376858e382162
      (head (compile-pack-source-occurrence (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) right) 7))
      (body))
    (rule parser-pack-fact-4233b1b9dc2c4bacfbc3bec641fadd5f690f6bbca587f2256c8d70bca140b1a5
      (head (compile-pack-source-occurrence (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) left) ref) 8))
      (body))
    (rule parser-pack-fact-1fbb29be4949ac7162d7ce6b19dcb0ef4c07571505c36c5522a3df63e217015c
      (head (compile-pack-source-occurrence (pp-inline-label 6e50278443333d05887f62a65e8219542b1ce8ece54cb882592ea7672904773c) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) right) eps) 9))
      (body))
    (rule parser-pack-fact-bc923c98985b3f1d7dbddd0d47973185015ff1596afb5373c27f2ee12c3fc40e
      (head (compile-pack-source-occurrence (pp-inline-label 6e817207322d301fda5fb885394deb23fb2a6e7ea92907dc33d32a376628b60a) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) star-empty) 0))
      (body))
    (rule parser-pack-fact-a164e41220f97830d7ae07d8b866013bb260d18066e6037de2531ed5d441cf9f
      (head (compile-pack-source-occurrence (pp-inline-label 74233964f80bdcb1d726a9579d9fff99d8d063764a3d2755483ca239977cbaa6) (pp-label (pp-def mm-category-proof_header_list) alt-right) 0))
      (body))
    (rule parser-pack-fact-6e2725cae4e955f133c455c63faefa47803204bcd8355fc763295fd4b416edb2
      (head (compile-pack-source-occurrence (pp-inline-label 74233964f80bdcb1d726a9579d9fff99d8d063764a3d2755483ca239977cbaa6) (pp-label (pp-sub (pp-def mm-category-proof_header_list) right) ref) 1))
      (body))
    (rule parser-pack-fact-71aefced385b01102166f0ef16f2f3e80495e471b0e2ad40e2680a55588fc8ad
      (head (compile-pack-source-occurrence (pp-inline-label 757f57fdb2ebd4d8246caebe0aec8eb1ccc4d5d9dce9a88efcec7148de91bf74) (pp-label (pp-def right-paren-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-2f08e3748df8e7f61842c23024dd67be737cd26c2a430ca19711355ea37c50ac
      (head (compile-pack-source-occurrence (pp-inline-label 757f57fdb2ebd4d8246caebe0aec8eb1ccc4d5d9dce9a88efcec7148de91bf74) (pp-label (pp-sub (pp-def right-paren-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-736909c275bdae825c4d7b8d6167024530e9fa50b26ca7c26bcf533c6debe977
      (head (compile-pack-source-occurrence (pp-inline-label 757f57fdb2ebd4d8246caebe0aec8eb1ccc4d5d9dce9a88efcec7148de91bf74) (pp-label (pp-sub (pp-def right-paren-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-24010b795ca3242ae8368ab5eb8ed71444b3885ce25d40639e3bd24300c1abda
      (head (compile-pack-source-occurrence (pp-inline-label 7626ad4c39b98eb6f71599df437134185878c2a74a90ed31b11a27a5e8fe4046) (pp-label (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) plus-more) 0))
      (body))
    (rule parser-pack-fact-f7120a4fd6bd4ac646c9875bcbbd2bcb350f5c2b358750c0c26e98e3068467b2
      (head (compile-pack-source-occurrence (pp-inline-label 7626ad4c39b98eb6f71599df437134185878c2a74a90ed31b11a27a5e8fe4046) (pp-label (pp-sub (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) body) class) 1))
      (body))
    (rule parser-pack-fact-808f42919cbe0e6931d218623e026f13ee2bd6b46b00b6ad6c95c96898e95524
      (head (compile-pack-source-occurrence (pp-inline-label 775e0c31c812cd8868036b6a2d953be810cce52108ed62a27a310749f68227f8) (pp-label (pp-def mm-category-disjoint_symbol_list) alt-right) 0))
      (body))
    (rule parser-pack-fact-73063003ec58682dca225277c460ecb539d07fe69e413284caa272e6de5f6482
      (head (compile-pack-source-occurrence (pp-inline-label 775e0c31c812cd8868036b6a2d953be810cce52108ed62a27a310749f68227f8) (pp-label (pp-sub (pp-def mm-category-disjoint_symbol_list) right) ref) 1))
      (body))
    (rule parser-pack-fact-549b1b08ba4b7e782909235205b04c5324965a6e9175d948dd379b24c49c2570
      (head (compile-pack-source-occurrence (pp-inline-label 78873b124e8d6e10dea0353f19cdae566493e793618ef0710676db077f0a5963) (pp-label (pp-def mm-production-statement_disjoint) node) 0))
      (body))
    (rule parser-pack-fact-5ab52675c37f1f1a80780a7ca2ff708fb3346ed1ba638c1d829df8f8cbac435e
      (head (compile-pack-source-occurrence (pp-inline-label 78873b124e8d6e10dea0353f19cdae566493e793618ef0710676db077f0a5963) (pp-label (pp-sub (pp-def mm-production-statement_disjoint) body) right) 1))
      (body))
    (rule parser-pack-fact-a86f21993fa6264403121dcf75c73bebbb905de34807272ac40aa1127b549ebd
      (head (compile-pack-source-occurrence (pp-inline-label 78873b124e8d6e10dea0353f19cdae566493e793618ef0710676db077f0a5963) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-ff3fd2cc7e2b19e87af745a201f66fadfe42a542ad99ece83c62c8c31e0a49b4
      (head (compile-pack-source-occurrence (pp-inline-label 78873b124e8d6e10dea0353f19cdae566493e793618ef0710676db077f0a5963) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) left) 3))
      (body))
    (rule parser-pack-fact-758b0e42817ee50d2708862d9bd33b3c6cf15d51d564a3a0f2c759fb4fe557f7
      (head (compile-pack-source-occurrence (pp-inline-label 78873b124e8d6e10dea0353f19cdae566493e793618ef0710676db077f0a5963) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-bcb516ddee0982d269b9a09f7819d8b23394a006e13aa7fdc6d174b17f29b536
      (head (compile-pack-source-occurrence (pp-inline-label 78873b124e8d6e10dea0353f19cdae566493e793618ef0710676db077f0a5963) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) right) 5))
      (body))
    (rule parser-pack-fact-d352d008b96bfe0a5546415d0f9443e1505f3fe4b40637cac9df280582b481bf
      (head (compile-pack-source-occurrence (pp-inline-label 78873b124e8d6e10dea0353f19cdae566493e793618ef0710676db077f0a5963) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-c4ad2469970d6617fe9234abfbd7f0bfa50f737bdd6c0e400c170beabf347e80
      (head (compile-pack-source-occurrence (pp-inline-label 78873b124e8d6e10dea0353f19cdae566493e793618ef0710676db077f0a5963) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) right) eps) 7))
      (body))
    (rule parser-pack-fact-3d76c040f5e0987153058ce326fe3ecd5e709aa8e892ee02c1a3185df8ead8f2
      (head (compile-pack-source-occurrence (pp-inline-label 7a22485051fbdb7f148150e005ce68a455e4b520873bcc93d2dc96f733c62221) (pp-label (pp-def kw-block-close) left) 0))
      (body))
    (rule parser-pack-fact-3422c8e8c8225469e76baf0a693708e223ab19c5de7d10f5fc0504c7ece76945
      (head (compile-pack-source-occurrence (pp-inline-label 7a22485051fbdb7f148150e005ce68a455e4b520873bcc93d2dc96f733c62221) (pp-label (pp-sub (pp-def kw-block-close) left) ref) 1))
      (body))
    (rule parser-pack-fact-04d450da154fd60dac53ce6133d3d9dbcf926bbf6688a4eb73e5dc0f625fcbb4
      (head (compile-pack-source-occurrence (pp-inline-label 7a22485051fbdb7f148150e005ce68a455e4b520873bcc93d2dc96f733c62221) (pp-label (pp-sub (pp-def kw-block-close) right) ref) 2))
      (body))
    (rule parser-pack-fact-ec61799ef73a8d6cb4bec4d96b537aa3f43781790171107c62919326a14f1d47
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-def mm-production-statement_theorem_normal) node) 0))
      (body))
    (rule parser-pack-fact-3e4cbfc81a0a01d1c4899015962c57c04649619e0f2587917cf16aa3d2a322b4
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-def mm-production-statement_theorem_normal) body) seq) 1))
      (body))
    (rule parser-pack-fact-665f8118e9e6751517fb9d5e94f2c74c9c22dacfba3ed8609d90893eff1e6766
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-1347eb2b96b43cb4832e74f3c87228510102dde4870680d962832ca647450fd5
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) 3))
      (body))
    (rule parser-pack-fact-fe073b8d5ed0e40a6bcbbe34530db25f09e0f47da31dacf7199ed3bf92b4cf15
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-9ae8eb09fd01488fb7a13155043051587747cab7babada35da91c1e7223bd0b4
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) seq) 5))
      (body))
    (rule parser-pack-fact-ceec88ecb34d98b8cb32bb4371e81d4fe4746aff5670e22f719a8fa5d25510f4
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-9129ce53e1b5176d84318cae3531e225d02ff21469d8c2d1a527b13048e10c47
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) 7))
      (body))
    (rule parser-pack-fact-794368147602c4a797a8b5739530ac3294e3f4991d51b02e4579e0930b19ca4a
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) left) ref) 8))
      (body))
    (rule parser-pack-fact-f03db2e48ac0ad1f2474e85d0ca1cdee8fbfaa0823a2e022b4ec8063c57f8201
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) left) 9))
      (body))
    (rule parser-pack-fact-d7d439bfeb169b7543ded06b0e0e7dcf98eaf7585993aadc14f4bbc659538860
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) left) ref) 10))
      (body))
    (rule parser-pack-fact-9720146b60fafd82534e3c9bbcab2fea8b94ff0e8626e9fac5edda518ed781f1
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) right) 11))
      (body))
    (rule parser-pack-fact-b5290a5ae2dee2c17a3ad11d412405094e880a0b3cd7a6e79816baf10280d668
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) left) ref) 12))
      (body))
    (rule parser-pack-fact-a8f26ec8ece77746d4389ded7bff73b710b56dcb85936e6fdfa3f0be0db86df2
      (head (compile-pack-source-occurrence (pp-inline-label 7aac656a5c5117f9ccdec4f66f5d365b8bd829e0799fc47a74b220a232b736b6) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) right) eps) 13))
      (body))
    (rule parser-pack-fact-dd4e47c717a5be01a157de8d6769f2daaf7b00d519474a3045d06f148e236fae
      (head (compile-pack-source-occurrence (pp-inline-label 7b159c3aebd5b2765c42d2238eb710613c13cf11772ee2274079d0c3441d2c04) (pp-label (pp-def mm-category-outer_database) alt-right) 0))
      (body))
    (rule parser-pack-fact-e4fed89fdf5b345d4745e682e4ca6954bd05f86ddf1832527a87283177026795
      (head (compile-pack-source-occurrence (pp-inline-label 7b159c3aebd5b2765c42d2238eb710613c13cf11772ee2274079d0c3441d2c04) (pp-label (pp-sub (pp-def mm-category-outer_database) right) ref) 1))
      (body))
    (rule parser-pack-fact-d520ada3b36b5dd32e1bf9df51f18932539790bf7d805bfa04afdff3f70670b9
      (head (compile-pack-source-occurrence (pp-inline-label 7c27338890eeddfd445c5ff3d8fe2e05073bbcb3510eaa947892d546cc8fd958) (pp-label (pp-def kw-include-open) left) 0))
      (body))
    (rule parser-pack-fact-9d23bb8cb6b69ac0bcf5e68fe38ae6b8856d4023cd6320d800bb250f7c892a27
      (head (compile-pack-source-occurrence (pp-inline-label 7c27338890eeddfd445c5ff3d8fe2e05073bbcb3510eaa947892d546cc8fd958) (pp-label (pp-sub (pp-def kw-include-open) left) ref) 1))
      (body))
    (rule parser-pack-fact-f9eb8a8439851f657f68ecc6f0195601ef80a06f73391df65abeced40bf49bf2
      (head (compile-pack-source-occurrence (pp-inline-label 7c27338890eeddfd445c5ff3d8fe2e05073bbcb3510eaa947892d546cc8fd958) (pp-label (pp-sub (pp-def kw-include-open) right) ref) 2))
      (body))
    (rule parser-pack-fact-f47b29bfbbbf36f51ba672db9f0e628396b1c056075f967e62986631c5f3cada
      (head (compile-pack-source-occurrence (pp-inline-label 8056acdd4be7de6976847e353beb7fd143c8f3a86ca81fd4e4eb88f262775da1) (pp-label (pp-def mm-production-statement_include) node) 0))
      (body))
    (rule parser-pack-fact-16afe7815fb3aed4cd2ebfacee53d22cacf86c5e5058ff4def7ce5e76df3f058
      (head (compile-pack-source-occurrence (pp-inline-label 8056acdd4be7de6976847e353beb7fd143c8f3a86ca81fd4e4eb88f262775da1) (pp-label (pp-sub (pp-def mm-production-statement_include) body) right) 1))
      (body))
    (rule parser-pack-fact-a1e967ec5809b8221cc1c3ed31f1e49dfe1d595995b00ac35bcb4ff74e8edf91
      (head (compile-pack-source-occurrence (pp-inline-label 8056acdd4be7de6976847e353beb7fd143c8f3a86ca81fd4e4eb88f262775da1) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_include) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-50f6fa662b3f2a97616ee6bfc45bce96268b0fad92c74d949020999bc75f0f49
      (head (compile-pack-source-occurrence (pp-inline-label 8056acdd4be7de6976847e353beb7fd143c8f3a86ca81fd4e4eb88f262775da1) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) left) 3))
      (body))
    (rule parser-pack-fact-fafac367cfb3ff89161d59ea3663a2f5b01ca2dec7f64eea0d996c1576a28d9b
      (head (compile-pack-source-occurrence (pp-inline-label 8056acdd4be7de6976847e353beb7fd143c8f3a86ca81fd4e4eb88f262775da1) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-67f8e39821c1aa9a33a8bfb58eb0ccfd4ec691dea69c0d287e60e1b819808706
      (head (compile-pack-source-occurrence (pp-inline-label 8056acdd4be7de6976847e353beb7fd143c8f3a86ca81fd4e4eb88f262775da1) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) right) 5))
      (body))
    (rule parser-pack-fact-d866a427471a9ad75aedd68e8041862a0946de7647589fa97abf3f3acef2e753
      (head (compile-pack-source-occurrence (pp-inline-label 8056acdd4be7de6976847e353beb7fd143c8f3a86ca81fd4e4eb88f262775da1) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-83d3781427562264b8c359862b17ed811daed5b1ce21f9bb78fcb4fa8860c8ce
      (head (compile-pack-source-occurrence (pp-inline-label 8056acdd4be7de6976847e353beb7fd143c8f3a86ca81fd4e4eb88f262775da1) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) right) eps) 7))
      (body))
    (rule parser-pack-fact-7e99a1c05680059e9082187a1224871b54fc2a6e6fc8411063016d0ab28190fe
      (head (compile-pack-source-occurrence (pp-inline-label 80b3e65136fdd40ca15429c7cd5a63d07670d098ffb3a07a6f303002628d923c) (pp-label (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) plus-one) 0))
      (body))
    (rule parser-pack-fact-c233226aee006ed7d2a976fbd729d77793edb1cb7fd30ec96849e7d2818e031f
      (head (compile-pack-source-occurrence (pp-inline-label 80b3e65136fdd40ca15429c7cd5a63d07670d098ffb3a07a6f303002628d923c) (pp-label (pp-sub (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) body) class) 1))
      (body))
    (rule parser-pack-fact-027d7fe1c351e07b216ce222eede2cad43c3657f335fcde4f49a921671e319ca
      (head (compile-pack-source-occurrence (pp-inline-label 8119e68213f3810fcbc160444babd29c48cde1b77485d5021f3bbb300967da0b) (pp-label (pp-def kw-axiom-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-f7c7f7f362801252a00217e70dda0eb16e44ff02991d3671ae58a72c4febd9ee
      (head (compile-pack-source-occurrence (pp-inline-label 8119e68213f3810fcbc160444babd29c48cde1b77485d5021f3bbb300967da0b) (pp-label (pp-sub (pp-def kw-axiom-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-bb69d8d5375e1f57496e209520279a9b20761cd7cef3c5c6f2b4e020789e4b3a
      (head (compile-pack-source-occurrence (pp-inline-label 8119e68213f3810fcbc160444babd29c48cde1b77485d5021f3bbb300967da0b) (pp-label (pp-sub (pp-def kw-axiom-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-a44b9343d6afdbc834c83834e1759d7eef1aa03674830268bf5ff16d591b1cb8
      (head (compile-pack-source-occurrence (pp-inline-label 835ce5c6f3b0d79698c65bc91ae525641ffe261c354bae27337afb4b9740df54) (pp-label (pp-def comment) node) 0))
      (body))
    (rule parser-pack-fact-e22445cd922b0239649d6211570ebb178bae799f247c685c8d978c06081303d2
      (head (compile-pack-source-occurrence (pp-inline-label 835ce5c6f3b0d79698c65bc91ae525641ffe261c354bae27337afb4b9740df54) (pp-label (pp-sub (pp-def comment) body) right) 1))
      (body))
    (rule parser-pack-fact-08e1c5596fd4a35be8c68b8ae1afaf384720a271483edc117fd5438efd23a0db
      (head (compile-pack-source-occurrence (pp-inline-label 835ce5c6f3b0d79698c65bc91ae525641ffe261c354bae27337afb4b9740df54) (pp-label (pp-sub (pp-sub (pp-def comment) body) left) lit) 2))
      (body))
    (rule parser-pack-fact-1d690a493a932b3ccb5acc652b605e7903140dc3a7607258fe5db02aecdedf29
      (head (compile-pack-source-occurrence (pp-inline-label 835ce5c6f3b0d79698c65bc91ae525641ffe261c354bae27337afb4b9740df54) (pp-label (pp-sub (pp-sub (pp-def comment) body) right) right) 3))
      (body))
    (rule parser-pack-fact-b481b817959089684dc0f569fa79afd7a999eaa7c56aa87fbd30f38d8cc2b264
      (head (compile-pack-source-occurrence (pp-inline-label 835ce5c6f3b0d79698c65bc91ae525641ffe261c354bae27337afb4b9740df54) (pp-label (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-6bae96be9191b4b67b0a0f6eab82f2ecca4c1e9c8883d5b4f86fe15e33308ba5
      (head (compile-pack-source-occurrence (pp-inline-label 835ce5c6f3b0d79698c65bc91ae525641ffe261c354bae27337afb4b9740df54) (pp-label (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) 5))
      (body))
    (rule parser-pack-fact-b35d3cb8c90cd23c1fc996a655df13abd7dcf1d368690c80f0fb42ee2d84f6ca
      (head (compile-pack-source-occurrence (pp-inline-label 835ce5c6f3b0d79698c65bc91ae525641ffe261c354bae27337afb4b9740df54) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) seq) 6))
      (body))
    (rule parser-pack-fact-23ee018ce56ba4bd80d6cd38f196e218c888a71b58a87c43fec2dd6b5293d3c2
      (head (compile-pack-source-occurrence (pp-inline-label 835ce5c6f3b0d79698c65bc91ae525641ffe261c354bae27337afb4b9740df54) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) left) lit) 7))
      (body))
    (rule parser-pack-fact-9559f5541589ca32094f89c967fa54a3310638bd02803d5543709fe806c770f7
      (head (compile-pack-source-occurrence (pp-inline-label 835ce5c6f3b0d79698c65bc91ae525641ffe261c354bae27337afb4b9740df54) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) right) class) 8))
      (body))
    (rule parser-pack-fact-76b374e330bda9425fcadb73a9224a4ead5262697b8144da4adf4662754f5c16
      (head (compile-pack-source-occurrence (pp-inline-label 84a7aede4214797b8153a700c39ae3f3a9dbe3b15de32b560039fb5308f53a75) (pp-label (pp-def mm-production-database_more) node) 0))
      (body))
    (rule parser-pack-fact-0e833945a87d05e2146a59b4cdcad2abf755f00c653c5124adcee8b73f574047
      (head (compile-pack-source-occurrence (pp-inline-label 84a7aede4214797b8153a700c39ae3f3a9dbe3b15de32b560039fb5308f53a75) (pp-label (pp-sub (pp-def mm-production-database_more) body) seq) 1))
      (body))
    (rule parser-pack-fact-93f86a4c2acd639923c603aa1fe56aee2e9e527b8f686daaf758828d257e59bb
      (head (compile-pack-source-occurrence (pp-inline-label 84a7aede4214797b8153a700c39ae3f3a9dbe3b15de32b560039fb5308f53a75) (pp-label (pp-sub (pp-sub (pp-def mm-production-database_more) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-136344eb43b1238c683d98144a8de13032adf6b5e9d2e5feed30c12ed6ede68f
      (head (compile-pack-source-occurrence (pp-inline-label 84a7aede4214797b8153a700c39ae3f3a9dbe3b15de32b560039fb5308f53a75) (pp-label (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) left) 3))
      (body))
    (rule parser-pack-fact-9e7f69d73c502458256856ee953bdaa84fa9c98c78125d3d6fba00a60e497623
      (head (compile-pack-source-occurrence (pp-inline-label 84a7aede4214797b8153a700c39ae3f3a9dbe3b15de32b560039fb5308f53a75) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-0f628c6bdc23fe4d037dae442bf463ef64375cb7d99d9c76b83c725b1b4b1837
      (head (compile-pack-source-occurrence (pp-inline-label 84a7aede4214797b8153a700c39ae3f3a9dbe3b15de32b560039fb5308f53a75) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) right) eps) 5))
      (body))
    (rule parser-pack-fact-76916f16e4d5da487f88a6ce453e01f1b1a0b04658285099fe349025e6de3f05
      (head (compile-pack-source-occurrence (pp-inline-label 87d35a0547aded5420aa736507f7c55b872dec28baa7db2deb5f2c3c652bb218) (pp-label (pp-def mm-production-header_empty) node) 0))
      (body))
    (rule parser-pack-fact-c6f64de24aae13109f793e2db4124ab144abb5e94b828cbf2120b711fb4de361
      (head (compile-pack-source-occurrence (pp-inline-label 87d35a0547aded5420aa736507f7c55b872dec28baa7db2deb5f2c3c652bb218) (pp-label (pp-sub (pp-def mm-production-header_empty) body) eps) 1))
      (body))
    (rule parser-pack-fact-30099a75a9a3a8d74fc438413c8e76a02740ff3030a4e0229839a1718f035af8
      (head (compile-pack-source-occurrence (pp-inline-label 8c95483f8e630649b8aeebc42e985c914815b49dacbc71585fbf4f3c41eebcec) (pp-label (pp-def mm-production-disjoint_symbols_two) node) 0))
      (body))
    (rule parser-pack-fact-59898289e53ac6b8e3eb93c5f303e001c5e69fefea4bbfe2470b16a167cd9608
      (head (compile-pack-source-occurrence (pp-inline-label 8c95483f8e630649b8aeebc42e985c914815b49dacbc71585fbf4f3c41eebcec) (pp-label (pp-sub (pp-def mm-production-disjoint_symbols_two) body) seq) 1))
      (body))
    (rule parser-pack-fact-d68682d7708a4563f97c534f90cb1610285ddfe1410d55d600bbc64736ca31ba
      (head (compile-pack-source-occurrence (pp-inline-label 8c95483f8e630649b8aeebc42e985c914815b49dacbc71585fbf4f3c41eebcec) (pp-label (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-99a359caa6a1e73bcea0e8316269296c5ee88af55d9a1eaed52a0579c9f0f88d
      (head (compile-pack-source-occurrence (pp-inline-label 8c95483f8e630649b8aeebc42e985c914815b49dacbc71585fbf4f3c41eebcec) (pp-label (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) left) 3))
      (body))
    (rule parser-pack-fact-4459fc912e30bf3b2fbb1f312b191c7d9d299aa03a0e81119b666a5ba0aca556
      (head (compile-pack-source-occurrence (pp-inline-label 8c95483f8e630649b8aeebc42e985c914815b49dacbc71585fbf4f3c41eebcec) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-e820e1e5446a6406cedaad2436937b2e0511e09ad5a2f63a699d2716268b571a
      (head (compile-pack-source-occurrence (pp-inline-label 8c95483f8e630649b8aeebc42e985c914815b49dacbc71585fbf4f3c41eebcec) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) right) eps) 5))
      (body))
    (rule parser-pack-fact-714ad079f847c7d7e30984948bbb39fd770477fd2a231ee0072543abea2bde84
      (head (compile-pack-source-occurrence (pp-inline-label 8dc17324b9979f4a098e568cc24a86d88a16752c3ac72390279906940ded66f5) (pp-label (pp-def symbol-token-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-7150a83526d8d295a98fc7ec3f5aaa14a6d1cf647f74e4b5e669e023ec06c2f8
      (head (compile-pack-source-occurrence (pp-inline-label 8dc17324b9979f4a098e568cc24a86d88a16752c3ac72390279906940ded66f5) (pp-label (pp-sub (pp-def symbol-token-lexeme) left) node) 1))
      (body))
    (rule parser-pack-fact-5a92a923d6c450510f1c934fdd61391e743b5b778f225c17fc3918e3a2e9cee0
      (head (compile-pack-source-occurrence (pp-inline-label 8dc17324b9979f4a098e568cc24a86d88a16752c3ac72390279906940ded66f5) (pp-label (pp-sub (pp-def symbol-token-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-175239bdf55a0e1bd59e673e6a3f2c3c456cc85d19fa4886c5d717027dd2ea44
      (head (compile-pack-source-occurrence (pp-inline-label 903ff10c953de70b529e5913ebd2103bffc652a345645e47a427613144f53214) (pp-label (pp-def comment-chunk) alt-left) 0))
      (body))
    (rule parser-pack-fact-860b600fb3761a592dbfafc3e189f289b5fdb90c43288ffa593a268bc2e7a4ce
      (head (compile-pack-source-occurrence (pp-inline-label 9129b74a03cc57054c3c7faf25dbf0fe39488705e94c06b34e2b963180066d4e) (pp-label (pp-def mm-category-proof_header_list) alt-left) 0))
      (body))
    (rule parser-pack-fact-fcb6a8c1b446bfa202a372231f07a943a3e6d70ce0e1a1cc0167bb27cac4b198
      (head (compile-pack-source-occurrence (pp-inline-label 9129b74a03cc57054c3c7faf25dbf0fe39488705e94c06b34e2b963180066d4e) (pp-label (pp-sub (pp-def mm-category-proof_header_list) left) ref) 1))
      (body))
    (rule parser-pack-fact-4053beab2dd7e60226114c984591774d7551dc39134dd2a04054be40ae7bc584
      (head (compile-pack-source-occurrence (pp-inline-label 914a1426ce08abfe495d34f42406127cc0bf0bb2ddd21ea434c10e9f272f16af) (pp-label (pp-def kw-float) left) 0))
      (body))
    (rule parser-pack-fact-8df97cdf7536ac01e2ab7d9ea75a328e28b3859c7404966cda2e50cf21ac1d81
      (head (compile-pack-source-occurrence (pp-inline-label 914a1426ce08abfe495d34f42406127cc0bf0bb2ddd21ea434c10e9f272f16af) (pp-label (pp-sub (pp-def kw-float) left) ref) 1))
      (body))
    (rule parser-pack-fact-55ac75589e66aace4cec1ac66b35f2c5c40814a248dce56bf5b6243adb6838f5
      (head (compile-pack-source-occurrence (pp-inline-label 914a1426ce08abfe495d34f42406127cc0bf0bb2ddd21ea434c10e9f272f16af) (pp-label (pp-sub (pp-def kw-float) right) ref) 2))
      (body))
    (rule parser-pack-fact-9c62585209b2f1b11741e2f77ced37e3742f57b442bff4c2117efb2d4dcb08db
      (head (compile-pack-source-occurrence (pp-inline-label 915c79d1994e22a3d6ea5760e7bbdc8fe255e195ff8fb381620b4d6e5c1c67d2) (pp-label (pp-def kw-variable) left) 0))
      (body))
    (rule parser-pack-fact-d5de122f24e5697ee359ce424cac20c3dd75f61f8d58f6827e858536dc9a9ded
      (head (compile-pack-source-occurrence (pp-inline-label 915c79d1994e22a3d6ea5760e7bbdc8fe255e195ff8fb381620b4d6e5c1c67d2) (pp-label (pp-sub (pp-def kw-variable) left) ref) 1))
      (body))
    (rule parser-pack-fact-835888d58f35d0f8a345cea4858b294ab3bcf13bc48d15d74ad9f5a3b74e4dce
      (head (compile-pack-source-occurrence (pp-inline-label 915c79d1994e22a3d6ea5760e7bbdc8fe255e195ff8fb381620b4d6e5c1c67d2) (pp-label (pp-sub (pp-def kw-variable) right) ref) 2))
      (body))
    (rule parser-pack-fact-d7e15a176609458d330af34a4bb021b7d0fe160933ac131fd5d2e3deefffb361
      (head (compile-pack-source-occurrence (pp-inline-label 93e0b1f12ae701279a774f720b02e33033d0c795adc6a8799889d9f9851bb87f) (pp-label (pp-sub (pp-sub (pp-def comment-chunk) right) left) star-cons) 0))
      (body))
    (rule parser-pack-fact-03fe8aaadc12b268093e837037db754cd5e85968cca398beda3967f5f0f7f8f0
      (head (compile-pack-source-occurrence (pp-inline-label 93e0b1f12ae701279a774f720b02e33033d0c795adc6a8799889d9f9851bb87f) (pp-label (pp-sub (pp-sub (pp-sub (pp-def comment-chunk) right) left) body) ref) 1))
      (body))
    (rule parser-pack-fact-e76a171510454228d1ced0b29f4238f4fd4c1339e5924f82e3c14a658c12a2be
      (head (compile-pack-source-occurrence (pp-inline-label 954944ccdbf468236a40f6b271b25d6aff7b748f6b5021a55d98f0f6f3718a87) (pp-label (pp-def mm-production-outer_statement_inner) node) 0))
      (body))
    (rule parser-pack-fact-87063c2804c1d4016b00c7bb52ca0dd73206d0d876c7fb899226aee790267472
      (head (compile-pack-source-occurrence (pp-inline-label 954944ccdbf468236a40f6b271b25d6aff7b748f6b5021a55d98f0f6f3718a87) (pp-label (pp-sub (pp-def mm-production-outer_statement_inner) body) left) 1))
      (body))
    (rule parser-pack-fact-494304b4cb0449c6ca18bba5478d33489a9e32d0f2961705212f3219129352e9
      (head (compile-pack-source-occurrence (pp-inline-label 954944ccdbf468236a40f6b271b25d6aff7b748f6b5021a55d98f0f6f3718a87) (pp-label (pp-sub (pp-sub (pp-def mm-production-outer_statement_inner) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-5b43e728a359794aa99e006b700f7577c4adb761bafd785c232221f1a54079ac
      (head (compile-pack-source-occurrence (pp-inline-label 954944ccdbf468236a40f6b271b25d6aff7b748f6b5021a55d98f0f6f3718a87) (pp-label (pp-sub (pp-sub (pp-def mm-production-outer_statement_inner) body) right) eps) 3))
      (body))
    (rule parser-pack-fact-fc6b12b423d592016a9dcf6f031069d463fdc2d942eac229351584ac30714296
      (head (compile-pack-source-occurrence (pp-inline-label 9e3b08388aec62654a8a6d42f0937ac1ecc5ac1ed2bfc7791a1d163d6c40c921) (pp-label (pp-def include-token-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-81a4b0d3261e76b32ff7bea2f9976d85fff4e1a638fa546ddb49eeb45589d48d
      (head (compile-pack-source-occurrence (pp-inline-label 9e3b08388aec62654a8a6d42f0937ac1ecc5ac1ed2bfc7791a1d163d6c40c921) (pp-label (pp-sub (pp-def include-token-lexeme) left) node) 1))
      (body))
    (rule parser-pack-fact-6fc7d336314a68ce3a3ebf6e9fdc5ec09cb119503fc4beee9cf1a6ae93707c8e
      (head (compile-pack-source-occurrence (pp-inline-label 9e3b08388aec62654a8a6d42f0937ac1ecc5ac1ed2bfc7791a1d163d6c40c921) (pp-label (pp-sub (pp-def include-token-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-44070fc5a9000fda474d0f1d47d586811d7ac6fd7a272f76d2b94e1bacc73f7e
      (head (compile-pack-source-occurrence (pp-inline-label a2c4ebb0fb8bcab6fabe6befb49bd9429c4f1a8a22065506e158501fdacd59ae) (pp-label (pp-def unknown-proof-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-aa5bb5d3a3e8b1210693421105bae78a868e797135b0107162dab7899b7e3662
      (head (compile-pack-source-occurrence (pp-inline-label a2c4ebb0fb8bcab6fabe6befb49bd9429c4f1a8a22065506e158501fdacd59ae) (pp-label (pp-sub (pp-def unknown-proof-lexeme) left) node) 1))
      (body))
    (rule parser-pack-fact-6dc4da92f39d972530158e6848412c2dfe3f0822288ac19c734f0f5c0e45b109
      (head (compile-pack-source-occurrence (pp-inline-label a2c4ebb0fb8bcab6fabe6befb49bd9429c4f1a8a22065506e158501fdacd59ae) (pp-label (pp-sub (pp-def unknown-proof-lexeme) right) ref) 3))
      (body))
    (rule parser-pack-fact-b91f3f649a29bcb1da63a8553c22e2b930d4bbdf203572903ed08aa7fa3dbe78
      (head (compile-pack-source-occurrence (pp-inline-label a2c4ebb0fb8bcab6fabe6befb49bd9429c4f1a8a22065506e158501fdacd59ae) (pp-label (pp-sub (pp-sub (pp-def unknown-proof-lexeme) left) body) lit) 2))
      (body))
    (rule parser-pack-fact-5c99b022cc83e79fd33fc1d7344f0b86c42b237eb934e551618d869a02b52585
      (head (compile-pack-source-occurrence (pp-inline-label a2d9b2ffe4fef0c566b39925d798d605c43e90057971a1d9eecf2d82f74bfdd3) (pp-label (pp-def mm-category-database) alt-left) 0))
      (body))
    (rule parser-pack-fact-2004042b05e57a55fa6dd119035c3aa62bcbeaf869ca00b6d8e67c5a22a0b241
      (head (compile-pack-source-occurrence (pp-inline-label a2d9b2ffe4fef0c566b39925d798d605c43e90057971a1d9eecf2d82f74bfdd3) (pp-label (pp-sub (pp-def mm-category-database) left) ref) 1))
      (body))
    (rule parser-pack-fact-dd17f9a1df30a156fdb34df4725ccb42bf6db45e433c6106184de232175891d0
      (head (compile-pack-source-occurrence (pp-inline-label a3892be823f272114001b8be3eb1d727c17a59f2cf68c4b73592739eb6f66974) (pp-label (pp-def kw-constant-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-3a880fd1dda9a22ed06a4a8910bbd142b606109585bb334419bf5fc154e3aff7
      (head (compile-pack-source-occurrence (pp-inline-label a3892be823f272114001b8be3eb1d727c17a59f2cf68c4b73592739eb6f66974) (pp-label (pp-sub (pp-def kw-constant-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-36847c68a864c90c93dd6189d30fcea0c6ba7bbf8e8e1002193c40a4a6210f66
      (head (compile-pack-source-occurrence (pp-inline-label a3892be823f272114001b8be3eb1d727c17a59f2cf68c4b73592739eb6f66974) (pp-label (pp-sub (pp-def kw-constant-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-a7229389de566ec0711d8f099394e8388442a69cbcb7e591ca15ba634279008f
      (head (compile-pack-source-occurrence (pp-inline-label a3ba37ab2788f0c6066347fcbdca296677c5752d777e0290106b98fe48b08978) (pp-label (pp-def mm-production-compressed_more) node) 0))
      (body))
    (rule parser-pack-fact-24cbb42c1f8f821021a3bec4eca684dc81e9d06c6961b1dbc35774d92e3f239a
      (head (compile-pack-source-occurrence (pp-inline-label a3ba37ab2788f0c6066347fcbdca296677c5752d777e0290106b98fe48b08978) (pp-label (pp-sub (pp-def mm-production-compressed_more) body) seq) 1))
      (body))
    (rule parser-pack-fact-dc051ea5bb06c96f0011f9914d50b15bfb410abcd8b7d73f2f688a524dd3cc60
      (head (compile-pack-source-occurrence (pp-inline-label a3ba37ab2788f0c6066347fcbdca296677c5752d777e0290106b98fe48b08978) (pp-label (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-7a105ef3f7ac7e36c9f5f2b78082b7d76073274a2436d107718cf86f0a1f1be4
      (head (compile-pack-source-occurrence (pp-inline-label a3ba37ab2788f0c6066347fcbdca296677c5752d777e0290106b98fe48b08978) (pp-label (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) left) 3))
      (body))
    (rule parser-pack-fact-57eb2044e6f3eae0325678bb181ad3e0eed6c8776411b6264b3d6320d3dbff18
      (head (compile-pack-source-occurrence (pp-inline-label a3ba37ab2788f0c6066347fcbdca296677c5752d777e0290106b98fe48b08978) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-dbc0915364e06620e7d0f31c8fb85aaadb77a05ad3db47fb83e033b284e7ae33
      (head (compile-pack-source-occurrence (pp-inline-label a3ba37ab2788f0c6066347fcbdca296677c5752d777e0290106b98fe48b08978) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) right) eps) 5))
      (body))
    (rule parser-pack-fact-6c9ee831c4e9f3f9a769415ea9292be078bb142d4683293101340d5543ef383a
      (head (compile-pack-source-occurrence (pp-inline-label a4a13cdce0c49c52cd766aed66bd5236a6338416403259fdb4ef55edbfecf018) (pp-label (pp-def comment-pair-unit) alt-left) 0))
      (body))
    (rule parser-pack-fact-2635c8673a19cfa827654cca749cb8679c755fad9ba9322d9af3caca68721555
      (head (compile-pack-source-occurrence (pp-inline-label a4a13cdce0c49c52cd766aed66bd5236a6338416403259fdb4ef55edbfecf018) (pp-label (pp-sub (pp-def comment-pair-unit) left) class) 1))
      (body))
    (rule parser-pack-fact-007b956e737c403a77079fb621499d40802c1f1a2c8976d2b75e657405406685
      (head (compile-pack-source-occurrence (pp-inline-label a53a6bc0da2529e6af09fc975bc1c293c2cb1c84782beadf965f70bc0474a0e9) (pp-label (pp-def mm-production-outer_database_more) node) 0))
      (body))
    (rule parser-pack-fact-8f3601a76668d0725ca411edb64bd84748f079ced243af1ca4a95b40b3a13454
      (head (compile-pack-source-occurrence (pp-inline-label a53a6bc0da2529e6af09fc975bc1c293c2cb1c84782beadf965f70bc0474a0e9) (pp-label (pp-sub (pp-def mm-production-outer_database_more) body) seq) 1))
      (body))
    (rule parser-pack-fact-9fbaabf42ad7ef65c024510badf0eaa441dd3af4c73c8728a0ac6c7b5a645e00
      (head (compile-pack-source-occurrence (pp-inline-label a53a6bc0da2529e6af09fc975bc1c293c2cb1c84782beadf965f70bc0474a0e9) (pp-label (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-ea16ed0d4435f78491bd2198ead9e2294bec923e091d5887eec596b5f2a6e35c
      (head (compile-pack-source-occurrence (pp-inline-label a53a6bc0da2529e6af09fc975bc1c293c2cb1c84782beadf965f70bc0474a0e9) (pp-label (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) left) 3))
      (body))
    (rule parser-pack-fact-fd6ac1869a64c62254d3d1ea5b69b583ccfcfe9c4848291bcf0d03e9c67aafa1
      (head (compile-pack-source-occurrence (pp-inline-label a53a6bc0da2529e6af09fc975bc1c293c2cb1c84782beadf965f70bc0474a0e9) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-73132688d480f56337565b012e13c62c728518ef3cc3e9f93a29b64040651a2f
      (head (compile-pack-source-occurrence (pp-inline-label a53a6bc0da2529e6af09fc975bc1c293c2cb1c84782beadf965f70bc0474a0e9) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) right) eps) 5))
      (body))
    (rule parser-pack-fact-445998bcaa2c1ba6377f26f50bda433c3f1b4f3e731d84849880545d02125530
      (head (compile-pack-source-occurrence (pp-inline-label a842fcf5959b6196102cac1830f7d01b073d49bae1be83fea6ed66d4cf4db776) (pp-label (pp-def mm-production-compressed_one) node) 0))
      (body))
    (rule parser-pack-fact-e167aadb2306dac023180197d69127be95f5eb1d18a229dec6f78bfa91b6610b
      (head (compile-pack-source-occurrence (pp-inline-label a842fcf5959b6196102cac1830f7d01b073d49bae1be83fea6ed66d4cf4db776) (pp-label (pp-sub (pp-def mm-production-compressed_one) body) left) 1))
      (body))
    (rule parser-pack-fact-a1b5ac857fb5250e3e9ba1f07ba32fe22990f62b5525bd0372a97346751544d0
      (head (compile-pack-source-occurrence (pp-inline-label a842fcf5959b6196102cac1830f7d01b073d49bae1be83fea6ed66d4cf4db776) (pp-label (pp-sub (pp-sub (pp-def mm-production-compressed_one) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-997b37b9a157ac64c36a52651ac08e0fbe451c9c04af1efdca16d0bc941820a2
      (head (compile-pack-source-occurrence (pp-inline-label a842fcf5959b6196102cac1830f7d01b073d49bae1be83fea6ed66d4cf4db776) (pp-label (pp-sub (pp-sub (pp-def mm-production-compressed_one) body) right) eps) 3))
      (body))
    (rule parser-pack-fact-29b76d9866567065c2e36c4b3506480c76be47d9fc062d912c1597aaa0e424cd
      (head (compile-pack-source-occurrence (pp-inline-label acbd5f666242cfbb41329b2bdd3492dd49382ae36428667e1225ecec0a3d70b5) (pp-label (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) plus-one) 0))
      (body))
    (rule parser-pack-fact-d5e8e1a65e61be74cc88fe9f5824607c9be686736252ea72875292b77d78bb4e
      (head (compile-pack-source-occurrence (pp-inline-label acbd5f666242cfbb41329b2bdd3492dd49382ae36428667e1225ecec0a3d70b5) (pp-label (pp-sub (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) body) class) 1))
      (body))
    (rule parser-pack-fact-71f038767073fa511d22886afa292eeb31846a9648ceba6f98dc12c00e5493e9
      (head (compile-pack-source-occurrence (pp-inline-label b011bb353d6c63d8187758061817d36ee2365c36f3cb2cbeecb86f6ac81a3c96) (pp-label (pp-def mm-category-disjoint_symbol_list) alt-left) 0))
      (body))
    (rule parser-pack-fact-26fcf586d1cd7e8da12d73b6f95b6b0ec2409dc6971421841d176846b73e9cc8
      (head (compile-pack-source-occurrence (pp-inline-label b011bb353d6c63d8187758061817d36ee2365c36f3cb2cbeecb86f6ac81a3c96) (pp-label (pp-sub (pp-def mm-category-disjoint_symbol_list) left) ref) 1))
      (body))
    (rule parser-pack-fact-45774a9b15a5b05406f968e1553903c38e91d6bd6709d8d86fddc4469f6d5a6a
      (head (compile-pack-source-occurrence (pp-inline-label b1eea1f0963b6a43a1232f6f0c5b6bf14be7566cdd9f82f41315f9e725c78010) (pp-label (pp-def whitespace1) plus-more) 0))
      (body))
    (rule parser-pack-fact-cf5d83bbc32ef1556efc5cc3909e4a852390377724d4b76fdae9a677ff0c65b4
      (head (compile-pack-source-occurrence (pp-inline-label b1eea1f0963b6a43a1232f6f0c5b6bf14be7566cdd9f82f41315f9e725c78010) (pp-label (pp-sub (pp-def whitespace1) body) class) 1))
      (body))
    (rule parser-pack-fact-f42314f2e56e1ee4565a8025883207b8ff6e676fca41af28dd0f5c0b305bcdcc
      (head (compile-pack-source-occurrence (pp-inline-label b3a70fa6305b072264e07b9b82a0c2a0a48cc5ca9d5bf78940c4386c8ff28059) (pp-label (pp-def mm-category-proof_list) alt-right) 0))
      (body))
    (rule parser-pack-fact-737fe0b889fb881a975b6cc7ef01935d0aaa2ebc9ded946fda3be1b4bc700677
      (head (compile-pack-source-occurrence (pp-inline-label b3a70fa6305b072264e07b9b82a0c2a0a48cc5ca9d5bf78940c4386c8ff28059) (pp-label (pp-sub (pp-def mm-category-proof_list) right) alt-right) 1))
      (body))
    (rule parser-pack-fact-73b7cf7934279e38df4c723faaea4cbd87422229c8bcaf32ef6370aada533546
      (head (compile-pack-source-occurrence (pp-inline-label b3a70fa6305b072264e07b9b82a0c2a0a48cc5ca9d5bf78940c4386c8ff28059) (pp-label (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) alt-right) 2))
      (body))
    (rule parser-pack-fact-359f3744be4c166424f2ad80ff522adc24d0bc4df898803aabb5ebe9a57e9d10
      (head (compile-pack-source-occurrence (pp-inline-label b3a70fa6305b072264e07b9b82a0c2a0a48cc5ca9d5bf78940c4386c8ff28059) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) right) ref) 3))
      (body))
    (rule parser-pack-fact-3862a9cd48eba078e908e5a75e618e263690442d3060652ddd5290edf18cd76d
      (head (compile-pack-source-occurrence (pp-inline-label b43f7cb1f7d889d9e1ed810cb72cd7d1e6bf8b1933a487a37216a69b48bf2f09) (pp-label (pp-def comment-chunk) alt-right) 0))
      (body))
    (rule parser-pack-fact-5e814331323c20a89c27d5ff88c4ad8f6011cb925d72684e7fc70599c7b75e57
      (head (compile-pack-source-occurrence (pp-inline-label b43f7cb1f7d889d9e1ed810cb72cd7d1e6bf8b1933a487a37216a69b48bf2f09) (pp-label (pp-sub (pp-def comment-chunk) right) seq) 1))
      (body))
    (rule parser-pack-fact-bedc0c70feb8600ae054a79a50f97ae1f459279d42e004d41e325f99d5ca9fba
      (head (compile-pack-source-occurrence (pp-inline-label b43f7cb1f7d889d9e1ed810cb72cd7d1e6bf8b1933a487a37216a69b48bf2f09) (pp-label (pp-sub (pp-sub (pp-def comment-chunk) right) right) char) 2))
      (body))
    (rule parser-pack-fact-b70c295b09b4f5cbb1322c4b5669bda526a82330a5784121bbded42caaadd2a7
      (head (compile-pack-source-occurrence (pp-inline-label b7d20d30bb3406c3a908ad66a195487984ce3c09a969f7d8d6af5644b9b4e4b5) (pp-label (pp-def token-separator) alt-left) 0))
      (body))
    (rule parser-pack-fact-0cb0582a092601f9d667943e3f12e77925d6ee1a59f1d003f978cb528b467f89
      (head (compile-pack-source-occurrence (pp-inline-label b7d20d30bb3406c3a908ad66a195487984ce3c09a969f7d8d6af5644b9b4e4b5) (pp-label (pp-sub (pp-def token-separator) left) class) 1))
      (body))
    (rule parser-pack-fact-ffd6ceb5637695aed7b990a5259b7180f26ad5c5bf27455341dc69c5f8789653
      (head (compile-pack-source-occurrence (pp-inline-label b9468f5bcb8a0858f1ae1155045025c7feee23cadbbf26744674bcf518770251) (pp-label (pp-def mm-category-outer_statement) alt-right) 0))
      (body))
    (rule parser-pack-fact-9707839d2e314a68ab7c1642573dd02c7b4f3593ca7c2b2d2c488a45c0895440
      (head (compile-pack-source-occurrence (pp-inline-label b9468f5bcb8a0858f1ae1155045025c7feee23cadbbf26744674bcf518770251) (pp-label (pp-sub (pp-def mm-category-outer_statement) right) ref) 1))
      (body))
    (rule parser-pack-fact-c596d5de1e850976ee802a7228b08517564b106ebbd19b97a1af25c5332f4325
      (head (compile-pack-source-occurrence (pp-inline-label bd50616733a528ed76c00f661497f7f343e83ec34478087411e360eee30ef653) (pp-label (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) plus-more) 0))
      (body))
    (rule parser-pack-fact-a21cefbeda3f26461ce2b35b40b5ed755f6962d95218189a6e052e092fe0a281
      (head (compile-pack-source-occurrence (pp-inline-label bd50616733a528ed76c00f661497f7f343e83ec34478087411e360eee30ef653) (pp-label (pp-sub (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) body) class) 1))
      (body))
    (rule parser-pack-fact-e1b42a820bbe8cf231ed920f8434f22247416f9441a8b03b467bb31facce7fbc
      (head (compile-pack-source-occurrence (pp-inline-label bedc5f31fb902cacf017b053814d05208ff6959017eaa9cad1c0b96a458fd0f4) (pp-label (pp-def mm-production-database_empty) node) 0))
      (body))
    (rule parser-pack-fact-3665259ea89d47f4d2f1faadf67aacdea98479b66c7a6df25a426f2278127c08
      (head (compile-pack-source-occurrence (pp-inline-label bedc5f31fb902cacf017b053814d05208ff6959017eaa9cad1c0b96a458fd0f4) (pp-label (pp-sub (pp-def mm-production-database_empty) body) eps) 1))
      (body))
    (rule parser-pack-fact-b026dbc00c49c8d43579a6031a8c6916ee0d260ff275765fec4ae4a46354bdaf
      (head (compile-pack-source-occurrence (pp-inline-label bfbdc59f73a802b8e94dc4efd299970e4b43e1d81b041de2fd8bb654b0b8e5b7) (pp-label (pp-def kw-variable-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-5ceecc5b713e10bd8eb51679d38388cfd9ddcfca35e274c63448c390f288a732
      (head (compile-pack-source-occurrence (pp-inline-label bfbdc59f73a802b8e94dc4efd299970e4b43e1d81b041de2fd8bb654b0b8e5b7) (pp-label (pp-sub (pp-def kw-variable-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-7b5027bc9ace2e08fb4a3426a55bf0ac7d61a3094cbee33a3931be9d2e8e60aa
      (head (compile-pack-source-occurrence (pp-inline-label bfbdc59f73a802b8e94dc4efd299970e4b43e1d81b041de2fd8bb654b0b8e5b7) (pp-label (pp-sub (pp-def kw-variable-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-97f3ca0765c441e8e21bb945a4a8a8cd14ac44dab578036111701593443d170e
      (head (compile-pack-source-occurrence (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-label (pp-def mm-production-statement_axiom) node) 0))
      (body))
    (rule parser-pack-fact-8f3aa53fedeafe458bea670d4a8215ac3fbd2d0c70b777de6e5c748b4ad765cb
      (head (compile-pack-source-occurrence (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-label (pp-sub (pp-def mm-production-statement_axiom) body) seq) 1))
      (body))
    (rule parser-pack-fact-ba75145bc4d2a7cb4599d6aec507ff2cb0ed682d372b4981834d2b834e2e21b6
      (head (compile-pack-source-occurrence (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-af28c619cbf76d18cbd5b6f549d3020946d48a5128cb0edf39f1a41082d374a3
      (head (compile-pack-source-occurrence (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) 3))
      (body))
    (rule parser-pack-fact-c51cfc983a37eea031f3537e40b9ccd11efa36350204721f617687c71af70804
      (head (compile-pack-source-occurrence (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-cfa34cb90082111f852b385123dc1782aa70d279a71b4b4d9deb17173622c0a3
      (head (compile-pack-source-occurrence (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) left) 5))
      (body))
    (rule parser-pack-fact-93fe2ee657d56f1c8fcd19084b4b50ab8a9f3c7e08d4edf7d14ddae923a96504
      (head (compile-pack-source-occurrence (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-91bddac1e38ddfa35e42c6a73d98cc42da5b1f4605d5e9ceef6fc9ac60c01237
      (head (compile-pack-source-occurrence (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) right) 7))
      (body))
    (rule parser-pack-fact-747a7f60912fcb418dc8b86e3d4d6423f682a8379f6f2f4887a0e740c426f74a
      (head (compile-pack-source-occurrence (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) left) ref) 8))
      (body))
    (rule parser-pack-fact-54977b947a01ed8f243f50d333c7cac3871d80bec90857ad5e22e502a18573f0
      (head (compile-pack-source-occurrence (pp-inline-label c56a92230fd7a7d60a0ffc66656f124d9d2ac045d6a4a4449eabfbd04271c1f3) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) right) eps) 9))
      (body))
    (rule parser-pack-fact-8c075046be71d321b6f299c79d215ca95befe1a1ce68570153ec1434717a4be1
      (head (compile-pack-source-occurrence (pp-inline-label c5f9c90109d2129deb0fe017e104af1894c596d980af20ffc7d039e0f55bb219) (pp-label (pp-def kw-include-close) left) 0))
      (body))
    (rule parser-pack-fact-498e66ac34e056bdb4996c49f4ce4827e31de7557cf910cd4e2890bc992ff710
      (head (compile-pack-source-occurrence (pp-inline-label c5f9c90109d2129deb0fe017e104af1894c596d980af20ffc7d039e0f55bb219) (pp-label (pp-sub (pp-def kw-include-close) left) ref) 1))
      (body))
    (rule parser-pack-fact-e6061989180e32e5ec67adb01186a601f2b5a5cad6792eeed95b4f88d714e43b
      (head (compile-pack-source-occurrence (pp-inline-label c5f9c90109d2129deb0fe017e104af1894c596d980af20ffc7d039e0f55bb219) (pp-label (pp-sub (pp-def kw-include-close) right) ref) 2))
      (body))
    (rule parser-pack-fact-b07b75da2ac1d0137200123189c441b171f3376fab015a525c5427489539a338
      (head (compile-pack-source-occurrence (pp-inline-label c76b3ffe99a01a8068a6b7eb165222bf82158c0ac178d95887618cb27f6496dd) (pp-label (pp-def kw-proof) left) 0))
      (body))
    (rule parser-pack-fact-042e7409df0b72392a6b150f8a0534a7965fd0ea2ae968f384c408107b2fa7e7
      (head (compile-pack-source-occurrence (pp-inline-label c76b3ffe99a01a8068a6b7eb165222bf82158c0ac178d95887618cb27f6496dd) (pp-label (pp-sub (pp-def kw-proof) left) ref) 1))
      (body))
    (rule parser-pack-fact-0f2ad7e8a60653fabbde8aa57d578442202a9a1540a40af62d7bef4baea9feed
      (head (compile-pack-source-occurrence (pp-inline-label c76b3ffe99a01a8068a6b7eb165222bf82158c0ac178d95887618cb27f6496dd) (pp-label (pp-sub (pp-def kw-proof) right) ref) 2))
      (body))
    (rule parser-pack-fact-9159e208cfc06a87a5652bf8093966da42616a70a78ab282ddd42889c44c07b6
      (head (compile-pack-source-occurrence (pp-inline-label c977563cbc95179722720b3e5bf4a5f2da56afbf18d02a936cf771f7fb2821cd) (pp-label (pp-def kw-proof-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-853f7d3d494153c8975624c34a5e84fab90c12dc277b9a04b8ff5d5c70e06470
      (head (compile-pack-source-occurrence (pp-inline-label c977563cbc95179722720b3e5bf4a5f2da56afbf18d02a936cf771f7fb2821cd) (pp-label (pp-sub (pp-def kw-proof-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-f6c3e0fb4b38743b31ea6da799fe9079b1f647a0855743e67b47dfa150ce892b
      (head (compile-pack-source-occurrence (pp-inline-label c977563cbc95179722720b3e5bf4a5f2da56afbf18d02a936cf771f7fb2821cd) (pp-label (pp-sub (pp-def kw-proof-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-6d7be650f5dd68f090564ff12958444f82aa1abcb3b7877785f79dac6ba0b56a
      (head (compile-pack-source-occurrence (pp-inline-label ca101022a915194182ca767fece26d79e3245d886570a7d118dd6e8c6c351475) (pp-label (pp-def mm-category-statement) alt-right) 0))
      (body))
    (rule parser-pack-fact-c890d0ede00477831d055315d4c4a55330c77ae8f3e0199732baf18a1c75626a
      (head (compile-pack-source-occurrence (pp-inline-label ca101022a915194182ca767fece26d79e3245d886570a7d118dd6e8c6c351475) (pp-label (pp-sub (pp-def mm-category-statement) right) alt-right) 1))
      (body))
    (rule parser-pack-fact-641697f3273923ac4a8e23751c4976c2a00a627f925fbbf286ba410de03fc2e5
      (head (compile-pack-source-occurrence (pp-inline-label ca101022a915194182ca767fece26d79e3245d886570a7d118dd6e8c6c351475) (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) right) alt-right) 2))
      (body))
    (rule parser-pack-fact-226a5d306b6e4df1d0a3de1e8b265f1e79a5735d11ff548fd512c12ebd767ae7
      (head (compile-pack-source-occurrence (pp-inline-label ca101022a915194182ca767fece26d79e3245d886570a7d118dd6e8c6c351475) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) alt-right) 3))
      (body))
    (rule parser-pack-fact-545a07157ba0f8408049b119e5cb8223adcb1d371cdd5fdbdc56a2ed5a21dfd2
      (head (compile-pack-source-occurrence (pp-inline-label ca101022a915194182ca767fece26d79e3245d886570a7d118dd6e8c6c351475) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) alt-right) 4))
      (body))
    (rule parser-pack-fact-1b42215ad8e5edfea7d4d714b273b683f3d79fc9e33bf869098063757cc34c29
      (head (compile-pack-source-occurrence (pp-inline-label ca101022a915194182ca767fece26d79e3245d886570a7d118dd6e8c6c351475) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) alt-left) 5))
      (body))
    (rule parser-pack-fact-c24a01871b449ae87cc5951bc9eb8bd0736c9e230dc0a4606d0d1ce09e244dd3
      (head (compile-pack-source-occurrence (pp-inline-label ca101022a915194182ca767fece26d79e3245d886570a7d118dd6e8c6c351475) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) left) ref) 6))
      (body))
    (rule parser-pack-fact-0d1d6dba924fa4a20d1e23fe48695eb4aebd4a2549dc378f3006653cfce97944
      (head (compile-pack-source-occurrence (pp-inline-label cb4b0d7c1e8a2602f3257baa96a31b8264f9d318bc23700d6521cd0d4c1da3b5) (pp-label (pp-def kw-end-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-60d1755a5ebc928fe18c9ac28591b98de40327c93517428b78427a625b0ed95b
      (head (compile-pack-source-occurrence (pp-inline-label cb4b0d7c1e8a2602f3257baa96a31b8264f9d318bc23700d6521cd0d4c1da3b5) (pp-label (pp-sub (pp-def kw-end-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-a69472342e68c823f1e65241daa01a24ff083ffa36811e425081ffc0461827a1
      (head (compile-pack-source-occurrence (pp-inline-label cb4b0d7c1e8a2602f3257baa96a31b8264f9d318bc23700d6521cd0d4c1da3b5) (pp-label (pp-sub (pp-def kw-end-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-6e61d2cad0aced9b5e29fbb4c37e9f7b6d5e0f261f901d225014254860241351
      (head (compile-pack-source-occurrence (pp-inline-label cea7c945352d1ddf47909cbe0cb1153a4a6a527fb6ada172aedb2d9224b5c4d0) (pp-label (pp-def unknown-proof) left) 0))
      (body))
    (rule parser-pack-fact-e1b56576bc9e9b9a93e4ec7ab31a427fbc170a0b8dfaeccd91337b4719ac86e5
      (head (compile-pack-source-occurrence (pp-inline-label cea7c945352d1ddf47909cbe0cb1153a4a6a527fb6ada172aedb2d9224b5c4d0) (pp-label (pp-sub (pp-def unknown-proof) left) ref) 1))
      (body))
    (rule parser-pack-fact-492401cb0378fdf8f1fbc90656f45b2beb498438c188acfcb2488a1057dfc8ce
      (head (compile-pack-source-occurrence (pp-inline-label cea7c945352d1ddf47909cbe0cb1153a4a6a527fb6ada172aedb2d9224b5c4d0) (pp-label (pp-sub (pp-def unknown-proof) right) ref) 2))
      (body))
    (rule parser-pack-fact-bacee8eb8374509b7bfe2c760e990553b844eaa7781f9da27d6b9652fe979922
      (head (compile-pack-source-occurrence (pp-inline-label d2ae8280fd00b82588ba9e6f862b4b2d7c964c2728c06672595c626a86929469) (pp-label (pp-def kw-theorem-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-90d0fdcdd5166a042efa6362b62f3f92c255c7ade2e90c0c8203455c68ed8f9c
      (head (compile-pack-source-occurrence (pp-inline-label d2ae8280fd00b82588ba9e6f862b4b2d7c964c2728c06672595c626a86929469) (pp-label (pp-sub (pp-def kw-theorem-lexeme) left) lit) 1))
      (body))
    (rule parser-pack-fact-1446f05ca1c87e891a5ff7d38cf6ad81d2610987dacc78b05965b712aec24720
      (head (compile-pack-source-occurrence (pp-inline-label d2ae8280fd00b82588ba9e6f862b4b2d7c964c2728c06672595c626a86929469) (pp-label (pp-sub (pp-def kw-theorem-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-805b66e0e8621afb37557886564e949824e8d6c89b05c858ce404d8b4f07e535
      (head (compile-pack-source-occurrence (pp-inline-label d348b73eefd79e9a950622081bc3182fa5bea7a9cffec03df8f2679fc3f616c6) (pp-label (pp-def label-token-lexeme) left) 0))
      (body))
    (rule parser-pack-fact-61eec2146dfc8b746da8436363e36dcbd1c155b13d1187031f520468642b3199
      (head (compile-pack-source-occurrence (pp-inline-label d348b73eefd79e9a950622081bc3182fa5bea7a9cffec03df8f2679fc3f616c6) (pp-label (pp-sub (pp-def label-token-lexeme) left) node) 1))
      (body))
    (rule parser-pack-fact-49d7bd8f5888c090d53f58d0310f0642a0d0ca3becfaf02f7c3f3b9168e0ee88
      (head (compile-pack-source-occurrence (pp-inline-label d348b73eefd79e9a950622081bc3182fa5bea7a9cffec03df8f2679fc3f616c6) (pp-label (pp-sub (pp-def label-token-lexeme) right) ref) 2))
      (body))
    (rule parser-pack-fact-aa1b814483402dc661d9e3bbe2189368eaf47576a0d0258f7a6f0e9e7d4aab33
      (head (compile-pack-source-occurrence (pp-inline-label d4d7dec498dbb322f5f1d48708800e142bfc8c223b07ac1db8317fbd2fe539a6) (pp-label (pp-def mm-production-symbols_more) node) 0))
      (body))
    (rule parser-pack-fact-cb1a354e1da0709cea16641124338dfa61c70782d384f04e94183812fee175e6
      (head (compile-pack-source-occurrence (pp-inline-label d4d7dec498dbb322f5f1d48708800e142bfc8c223b07ac1db8317fbd2fe539a6) (pp-label (pp-sub (pp-def mm-production-symbols_more) body) seq) 1))
      (body))
    (rule parser-pack-fact-a3f4536d3f48b8a740f96d9fac156aaa5a20e8c744ae9b65e904ae00edf73422
      (head (compile-pack-source-occurrence (pp-inline-label d4d7dec498dbb322f5f1d48708800e142bfc8c223b07ac1db8317fbd2fe539a6) (pp-label (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-7259f65116050efc7fedf5f0c4b3b4dba22de5a055a6a7fd470f8021d6467dad
      (head (compile-pack-source-occurrence (pp-inline-label d4d7dec498dbb322f5f1d48708800e142bfc8c223b07ac1db8317fbd2fe539a6) (pp-label (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) left) 3))
      (body))
    (rule parser-pack-fact-1ea4eb3114da41bb28436dda7bd574bff3fa4b7d3980699005da1f09d118aa2f
      (head (compile-pack-source-occurrence (pp-inline-label d4d7dec498dbb322f5f1d48708800e142bfc8c223b07ac1db8317fbd2fe539a6) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-4bd4218a3ff513dac8d88a4874b00e78e5414b8f59723e055408d2630a83927f
      (head (compile-pack-source-occurrence (pp-inline-label d4d7dec498dbb322f5f1d48708800e142bfc8c223b07ac1db8317fbd2fe539a6) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) right) eps) 5))
      (body))
    (rule parser-pack-fact-93960257506e8fe3e7093342e036625e96bf4bf4a22d5871a8fad2c9582e92f1
      (head (compile-pack-source-occurrence (pp-inline-label d961949d195259714ef1fdb5d5f07180a5bc25c11e4113f4a913abf83c592b69) (pp-label (pp-def kw-constant) left) 0))
      (body))
    (rule parser-pack-fact-3cd98cb26007aa9cc2f2ad3a337798948598bfe5a9eabccdbeacf67a7de59889
      (head (compile-pack-source-occurrence (pp-inline-label d961949d195259714ef1fdb5d5f07180a5bc25c11e4113f4a913abf83c592b69) (pp-label (pp-sub (pp-def kw-constant) left) ref) 1))
      (body))
    (rule parser-pack-fact-c93210bafa1c268b0944125e4dc0567d8574fa6a38c5d5edcd17e9a7bd117714
      (head (compile-pack-source-occurrence (pp-inline-label d961949d195259714ef1fdb5d5f07180a5bc25c11e4113f4a913abf83c592b69) (pp-label (pp-sub (pp-def kw-constant) right) ref) 2))
      (body))
    (rule parser-pack-fact-359cf5473b209c70be331dadb1415f9b1307c2a6c25aacb3995061dfe33bd086
      (head (compile-pack-source-occurrence (pp-inline-label e73aa717c26f58dc410198f768bd7020a42445f425ca13901a660d71c21a7a20) (pp-label (pp-def label-token) left) 0))
      (body))
    (rule parser-pack-fact-1c7728a6aafa29064342fc2006e70b05a6cede9427ce2c64984ba2a8d8ca4ef3
      (head (compile-pack-source-occurrence (pp-inline-label e73aa717c26f58dc410198f768bd7020a42445f425ca13901a660d71c21a7a20) (pp-label (pp-sub (pp-def label-token) left) ref) 1))
      (body))
    (rule parser-pack-fact-468f064085047a7f98f14128177aade7c14bf40a77dbeadde62ddc4985b1b98e
      (head (compile-pack-source-occurrence (pp-inline-label e73aa717c26f58dc410198f768bd7020a42445f425ca13901a660d71c21a7a20) (pp-label (pp-sub (pp-def label-token) right) ref) 2))
      (body))
    (rule parser-pack-fact-0631095ea87da6cb04b2197c79b395443ea02f84def4511e40e211345817ec17
      (head (compile-pack-source-occurrence (pp-inline-label ee8fc33b21989bffe0b6cc70186ece9df106ab33196a30a847e1520ac1ebefd0) (pp-label (pp-def token-separator) alt-right) 0))
      (body))
    (rule parser-pack-fact-27143fb62c5fccc652b08318a420183d43fe7cc45933a60b902a9eaf232b8924
      (head (compile-pack-source-occurrence (pp-inline-label ee8fc33b21989bffe0b6cc70186ece9df106ab33196a30a847e1520ac1ebefd0) (pp-label (pp-sub (pp-def token-separator) right) eof) 1))
      (body))
    (rule parser-pack-fact-288f741c86104c6c41e2a0dd40d7661e1d6794cf449417493c7aa9f2049be70a
      (head (compile-pack-source-occurrence (pp-inline-label ee9c4d513861c844edfbe77cb5c91188b407e6b3fc83c716fd1e3f094e17f2e9) (pp-label (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) plus-more) 0))
      (body))
    (rule parser-pack-fact-1f87bb38cadd6dfccd634470c0ed6abe3712009c629bf0a6bbf549a754266dc3
      (head (compile-pack-source-occurrence (pp-inline-label ee9c4d513861c844edfbe77cb5c91188b407e6b3fc83c716fd1e3f094e17f2e9) (pp-label (pp-sub (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) body) class) 1))
      (body))
    (rule parser-pack-fact-ba27950ab6023eb563afd5e28cc6af043ad92971c12a33e52c5f30db5b51d249
      (head (compile-pack-source-occurrence (pp-inline-label f0cfd7736b6356f172e39753e7d4d256689ecd89af3ce34e2bee8d2e2e5059b6) (pp-label (pp-def comment-pair-unit) alt-right) 0))
      (body))
    (rule parser-pack-fact-8f6586f9e66f7fb284be00fca308f1da2f2eac82065f7aefb91edd6f41bd5959
      (head (compile-pack-source-occurrence (pp-inline-label f0cfd7736b6356f172e39753e7d4d256689ecd89af3ce34e2bee8d2e2e5059b6) (pp-label (pp-sub (pp-def comment-pair-unit) right) seq) 1))
      (body))
    (rule parser-pack-fact-e6a88bce34aad3e9176d7919ef733aa19389141c22d6703be76c39b33650fbad
      (head (compile-pack-source-occurrence (pp-inline-label f0cfd7736b6356f172e39753e7d4d256689ecd89af3ce34e2bee8d2e2e5059b6) (pp-label (pp-sub (pp-sub (pp-def comment-pair-unit) right) left) char) 2))
      (body))
    (rule parser-pack-fact-fab389d23d627a1ba282df577304fe878787730ef8cf04eacc6ef143a41000e9
      (head (compile-pack-source-occurrence (pp-inline-label f0cfd7736b6356f172e39753e7d4d256689ecd89af3ce34e2bee8d2e2e5059b6) (pp-label (pp-sub (pp-sub (pp-def comment-pair-unit) right) right) class) 3))
      (body))
    (rule parser-pack-fact-60c09986350b19ea485d0ad9bcf5d81e4c0fa1a6203f14fbfc7111dfb45493d7
      (head (compile-pack-source-occurrence (pp-inline-label f0e62b1dcf6cbd1e02dfd3c3f88495e82fb4849ca455d3b86ea4d9eb238e9413) (pp-label (pp-def include-token) left) 0))
      (body))
    (rule parser-pack-fact-aea88dafea7e245a5dab7614b467a1a83e7a847987561abe20b105339dff70aa
      (head (compile-pack-source-occurrence (pp-inline-label f0e62b1dcf6cbd1e02dfd3c3f88495e82fb4849ca455d3b86ea4d9eb238e9413) (pp-label (pp-sub (pp-def include-token) left) ref) 1))
      (body))
    (rule parser-pack-fact-28326414d227d29e65e64af9b39d60df8caeadd230654f929a1fa28d0d5cbd48
      (head (compile-pack-source-occurrence (pp-inline-label f0e62b1dcf6cbd1e02dfd3c3f88495e82fb4849ca455d3b86ea4d9eb238e9413) (pp-label (pp-sub (pp-def include-token) right) ref) 2))
      (body))
    (rule parser-pack-fact-7df5b0fea959fa10292bcea2fefb75465752acd4a47e4ab49b39e35960c4d893
      (head (compile-pack-source-occurrence (pp-inline-label f2b012df5229ba4e91d1208ee41d5ebfd75eda1e5c84aa2c9f59de9acab233c4) (pp-label (pp-def mm-category-statement) alt-right) 0))
      (body))
    (rule parser-pack-fact-bbf4f18f56631b20820cf5a02d07990085b5be46277647fd124f3cca1bc6c957
      (head (compile-pack-source-occurrence (pp-inline-label f2b012df5229ba4e91d1208ee41d5ebfd75eda1e5c84aa2c9f59de9acab233c4) (pp-label (pp-sub (pp-def mm-category-statement) right) alt-right) 1))
      (body))
    (rule parser-pack-fact-fb186bd1c057c0c68ac228e50648922a1dd6e361b8ea5e442a87d6c493fea8ab
      (head (compile-pack-source-occurrence (pp-inline-label f2b012df5229ba4e91d1208ee41d5ebfd75eda1e5c84aa2c9f59de9acab233c4) (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) right) alt-right) 2))
      (body))
    (rule parser-pack-fact-80c6f21fc6604355695daec7b57a79103d05cd511fefc68a589f04a07f6abf00
      (head (compile-pack-source-occurrence (pp-inline-label f2b012df5229ba4e91d1208ee41d5ebfd75eda1e5c84aa2c9f59de9acab233c4) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) alt-right) 3))
      (body))
    (rule parser-pack-fact-7cc34ce5f78198c0282cb0a4af0121a35a16ead19d5af742316ac4c293b7e40f
      (head (compile-pack-source-occurrence (pp-inline-label f2b012df5229ba4e91d1208ee41d5ebfd75eda1e5c84aa2c9f59de9acab233c4) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) alt-left) 4))
      (body))
    (rule parser-pack-fact-febe1fcf0b1b0095aa222df5ec57b09f70fab7a2456fb02db501fb86bb038ae1
      (head (compile-pack-source-occurrence (pp-inline-label f2b012df5229ba4e91d1208ee41d5ebfd75eda1e5c84aa2c9f59de9acab233c4) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) left) ref) 5))
      (body))
    (rule parser-pack-fact-d3d201b09cafbec34c5b8f27ee01cb29e6f32e8fa01bc495f27fa7053dfe8bcb
      (head (compile-pack-source-occurrence (pp-inline-label f75c1a02a76ef66275a9f8d417557f9e76a750c7b682b9b83cccf69fdae8fb09) (pp-label (pp-def database) node) 0))
      (body))
    (rule parser-pack-fact-4c1f58afbb120dc733e8c262324ad8b1a76f1199ce82236253a2f1a8c3a0a5de
      (head (compile-pack-source-occurrence (pp-inline-label f75c1a02a76ef66275a9f8d417557f9e76a750c7b682b9b83cccf69fdae8fb09) (pp-label (pp-sub (pp-def database) body) right) 1))
      (body))
    (rule parser-pack-fact-5cc74ca655a913e90a6164c989f4e0bcaa77121973bc36172f4c18c444f6c242
      (head (compile-pack-source-occurrence (pp-inline-label f75c1a02a76ef66275a9f8d417557f9e76a750c7b682b9b83cccf69fdae8fb09) (pp-label (pp-sub (pp-sub (pp-def database) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-1fe6016dc08e256f0ca4137ef05371f23977f787e16ea0cab8888dff7694d3e0
      (head (compile-pack-source-occurrence (pp-inline-label f75c1a02a76ef66275a9f8d417557f9e76a750c7b682b9b83cccf69fdae8fb09) (pp-label (pp-sub (pp-sub (pp-def database) body) right) left) 3))
      (body))
    (rule parser-pack-fact-d66fab95bc4eb5f264a2d04469468bbed4d8a1cf168a172f2665128870bfa109
      (head (compile-pack-source-occurrence (pp-inline-label f75c1a02a76ef66275a9f8d417557f9e76a750c7b682b9b83cccf69fdae8fb09) (pp-label (pp-sub (pp-sub (pp-sub (pp-def database) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-bc9139a3d566d751d4c5b84122f35f36d3bbcabc6ad18daaf61619530cbc08e8
      (head (compile-pack-source-occurrence (pp-inline-label f75c1a02a76ef66275a9f8d417557f9e76a750c7b682b9b83cccf69fdae8fb09) (pp-label (pp-sub (pp-sub (pp-sub (pp-def database) body) right) right) eof) 5))
      (body))
    (rule parser-pack-fact-162b444303fe75a8ed1da892dc89cce63c9d2b7971497b2f8d2987d5fe287e59
      (head (compile-pack-source-occurrence (pp-inline-label facb0b5e476539290231dd2c120023f11fab01abb666b0c3dde3f5fb31638121) (pp-label (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) plus-one) 0))
      (body))
    (rule parser-pack-fact-ec3f6a066d2032a29f9de46c88e14da4fd0bb8d2642b0933da197ec0eb4e1b9d
      (head (compile-pack-source-occurrence (pp-inline-label facb0b5e476539290231dd2c120023f11fab01abb666b0c3dde3f5fb31638121) (pp-label (pp-sub (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) body) class) 1))
      (body))
    (rule parser-pack-fact-6da8111a005abc4f59e81c5af858049c7e84a2d64d631490bfee6ce80747d66c
      (head (compile-pack-source-occurrence (pp-inline-label fb2a50d52cd37c4c436af699d0240736fede9ce456c121ea47f31e2264fe633e) (pp-label (pp-def mm-category-compressed_word_list) alt-left) 0))
      (body))
    (rule parser-pack-fact-c64b90843a0175ed3b30cb9ebe2278fb57f76b8cd0e98ff8265125f637074c5f
      (head (compile-pack-source-occurrence (pp-inline-label fb2a50d52cd37c4c436af699d0240736fede9ce456c121ea47f31e2264fe633e) (pp-label (pp-sub (pp-def mm-category-compressed_word_list) left) ref) 1))
      (body))
    (rule parser-pack-fact-6ecf3f45480de14d47606eb2d65154edfe11cc7577d5b01981600301a6440869
      (head (compile-pack-source-occurrence (pp-inline-label fc5624e290af249b88c6eab39475466fa15d7c4fc3fb297d8001ff00fa7a4aa3) (pp-label (pp-def mm-category-symbol_list) alt-left) 0))
      (body))
    (rule parser-pack-fact-0e3fa6d961014de5c5452726966d31d6224e04dca4b8315cdb2ba75b10d3cb56
      (head (compile-pack-source-occurrence (pp-inline-label fc5624e290af249b88c6eab39475466fa15d7c4fc3fb297d8001ff00fa7a4aa3) (pp-label (pp-sub (pp-def mm-category-symbol_list) left) ref) 1))
      (body))
    (rule parser-pack-fact-167f8a17110876800dfa72ced0c9853890b9bf11b975428be18a2fe404b9bdee
      (head (compile-pack-source-occurrence (pp-inline-label fe786a044b8a590ab3bbe410c77a543e5da3f8357e6a298f6daadee652cb0369) (pp-label (pp-def kw-essential) left) 0))
      (body))
    (rule parser-pack-fact-d33b6c51f81749b94bc7007026f79e04ebfa312972bc8dc5ecf51cd2a47e6fbf
      (head (compile-pack-source-occurrence (pp-inline-label fe786a044b8a590ab3bbe410c77a543e5da3f8357e6a298f6daadee652cb0369) (pp-label (pp-sub (pp-def kw-essential) left) ref) 1))
      (body))
    (rule parser-pack-fact-ae0daeabe4c20b4c1694a95c1510d85128700f8c973de53371eb59b5116e0ac0
      (head (compile-pack-source-occurrence (pp-inline-label fe786a044b8a590ab3bbe410c77a543e5da3f8357e6a298f6daadee652cb0369) (pp-label (pp-sub (pp-def kw-essential) right) ref) 2))
      (body))
    (rule parser-pack-fact-4f14a37e57a36961134ffed87ceed61fe954595012f39ac900d05ebc02d5ea95
      (head (compile-pack-source-occurrence (pp-inline-label ff63cc9a997bf1a5736ff303e1b6ce090368b2f6bbe951cd0e04e7ef7411a27c) (pp-label (pp-def mm-production-header_more) node) 0))
      (body))
    (rule parser-pack-fact-4ea92c81bf20433ba6e277746b72d9aae6005d4a70627913ca5344980be63113
      (head (compile-pack-source-occurrence (pp-inline-label ff63cc9a997bf1a5736ff303e1b6ce090368b2f6bbe951cd0e04e7ef7411a27c) (pp-label (pp-sub (pp-def mm-production-header_more) body) seq) 1))
      (body))
    (rule parser-pack-fact-fb1d6718a237141785b5e04c00147f25bba86301d7e2bf43b840d6adf82c9fd5
      (head (compile-pack-source-occurrence (pp-inline-label ff63cc9a997bf1a5736ff303e1b6ce090368b2f6bbe951cd0e04e7ef7411a27c) (pp-label (pp-sub (pp-sub (pp-def mm-production-header_more) body) left) ref) 2))
      (body))
    (rule parser-pack-fact-27b15d4456fe0b7adc4de5889dcdc85abd9046ec0543c4cfc3b5f9e2fa328563
      (head (compile-pack-source-occurrence (pp-inline-label ff63cc9a997bf1a5736ff303e1b6ce090368b2f6bbe951cd0e04e7ef7411a27c) (pp-label (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) left) 3))
      (body))
    (rule parser-pack-fact-c2f6b10af9ef2caef26a26471901f21d00d0973ed843f5b2eab4fe82e7d0b850
      (head (compile-pack-source-occurrence (pp-inline-label ff63cc9a997bf1a5736ff303e1b6ce090368b2f6bbe951cd0e04e7ef7411a27c) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) left) ref) 4))
      (body))
    (rule parser-pack-fact-ec1036cd8ac268c2af0eb797dc6a077b67493eea61f14f92c56469fab6acbbde
      (head (compile-pack-source-occurrence (pp-inline-label ff63cc9a997bf1a5736ff303e1b6ce090368b2f6bbe951cd0e04e7ef7411a27c) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) right) eps) 5))
      (body))
    (rule parser-pack-fact-4f7140c96a984385739e7f7772135cb97cfebcdd3f4c61255ea670ef2503dad2
      (head (compile-pack-source-occurrence (pp-inline-label ffa64c09911433009ca6ca56003e28054938d5043aab14ef46b5829949c4bf48) (pp-label (pp-def mm-category-statement) alt-right) 0))
      (body))
    (rule parser-pack-fact-14a097de29e9f19bbd3b1a2b43087944e50e159c6f42d09bd8f1d1719b63a576
      (head (compile-pack-source-occurrence (pp-inline-label ffa64c09911433009ca6ca56003e28054938d5043aab14ef46b5829949c4bf48) (pp-label (pp-sub (pp-def mm-category-statement) right) alt-right) 1))
      (body))
    (rule parser-pack-fact-dcb4ae1e042a8b4fde9938f800f49375c0a2400b0a612830c3320d3eb7a6e57c
      (head (compile-pack-source-occurrence (pp-inline-label ffa64c09911433009ca6ca56003e28054938d5043aab14ef46b5829949c4bf48) (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) right) alt-right) 2))
      (body))
    (rule parser-pack-fact-317ab57cc21804cd295e7403ce546228427371bdf2f67ddeb73f521b8968416b
      (head (compile-pack-source-occurrence (pp-inline-label ffa64c09911433009ca6ca56003e28054938d5043aab14ef46b5829949c4bf48) (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) alt-left) 3))
      (body))
    (rule parser-pack-fact-28e5ab2e8d9bccc6928aa1edc606345172ec663d37f2476f91153d6df0a76c9d
      (head (compile-pack-source-occurrence (pp-inline-label ffa64c09911433009ca6ca56003e28054938d5043aab14ef46b5829949c4bf48) (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) left) ref) 4))
      (body))
  ))
