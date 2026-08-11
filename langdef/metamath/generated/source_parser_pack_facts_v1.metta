; generated ParserPack facts for staged compiler GSLTs
(gslt-presentation-v1 GeneratedParserPackFactsV1-f5ad6cf1ae8f0dc9d6b229144b32096bb2ff868e2db388384cf9d3d1b5ec6dca
  (signature
    (operator compile-pack-production 2)
    (operator cp 1)
    (operator pa-apply 2)
    (operator pa-cons 2)
    (operator pa-const 1)
    (operator pa-slot 1)
    (operator pp-def 1)
    (operator pp-items-cons 2)
    (operator pp-label 2)
    (operator pp-nonterminal 1)
    (operator pp-production 4)
    (operator pp-sub 2)
    (operator pp-terminal 1)
    (operator pp-terminal-char 1)
    (operator pp-terminal-class 1)
    (operator q-succ 1)
  )
  (equations)
  (rewrites
    (rule parser-pack-fact-6832ae7f524d1fc571ecb4d1261ed263aa6981411f56a2f83989f80cf3047bfc
      (head (compile-pack-production comment (pp-production (pp-label (pp-def comment) node) (pp-def comment) (pp-items-cons (pp-nonterminal (pp-sub (pp-def comment) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const comment) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-7f26acf9b9fdc16082712f05efb4de095a0edd7a542d4d51be7a3903591f15a6
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-def comment) body) right) (pp-sub (pp-def comment) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment) body) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-9a8f3f8b06796bf4e7c8c490733bdd6bf67e83377533b57aa95b97d831020e2d
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-def comment) body) left) lit) (pp-sub (pp-sub (pp-def comment) body) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 40))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-548c8f974e1b1ab3749bc867e9dddf53bbd874cb87e5992dad8ac60bd9b567d3
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-def comment) body) right) right) (pp-sub (pp-sub (pp-def comment) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-1dc620db78979f123ba556243add7b255c91346435f32cd8dea5acb18d598083
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) left) (pp-items-cons (pp-nonterminal (pp-def whitespace1)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a622ca66d70a32d7a834a664abe216c8d75d536cb9d207569dda7607b1901b2b
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-c1e2d21a8c0adeed9e41f495fcf989c1d7dfba19cf2826f6f20abbf4c9f9c1be
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) star-cons) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-a6292b29850f15a1679b389ad531c008a435a279f24e98f8a3e3fd2f85cbd6fc
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) star-empty) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-41cff173f6863cc4389622f5a8247cf240ddf399e80daaf83374d8447ef5f695
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) seq) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-4baf8a881f23a512cda23a4b9ec00edcf599fa71f00dcbe511cdcff18c475be8
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) seq) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-cd16c5b024fb3a4d5a8ce5e74edc5c4f14feaff3c54eda9ab1da099ce27aa62f
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) left) lit) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 41))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-4075a6b4be136b51583f2ee03d0057fe23c02f4a9dc423fcbf680e28578939d4
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) right) class) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) right) right) (pp-items-cons (pp-terminal (pp-terminal-class whitespace)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2a651e131acf7580bfa4ded17ad12829b615ae522d79a9b61a19fe3bc9ebacea
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) left) (pp-items-cons (pp-nonterminal (pp-def comment-chunk)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-6f27816ef0f5f28e3ca3e41ffa912369ae74f3eb0520ad14622d2e2a6f7b03e1
      (head (compile-pack-production comment (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) right) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def comment) body) right) right) left) body) right) (pp-items-cons (pp-nonterminal (pp-def whitespace1)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7c9b2bd765ec9d3932e8d564b4326094139c83aa40df7dd0ff65a101884136c6
      (head (compile-pack-production comment-chunk (pp-production (pp-label (pp-def comment-chunk) alt-left) (pp-def comment-chunk) (pp-items-cons (pp-nonterminal (pp-sub (pp-def comment-chunk) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-6ea8cc91c3054df0d0b3eefc48096a78476d8b904e80aed3f2143139fa70e0dd
      (head (compile-pack-production comment-chunk (pp-production (pp-label (pp-def comment-chunk) alt-right) (pp-def comment-chunk) (pp-items-cons (pp-nonterminal (pp-sub (pp-def comment-chunk) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-9b25a13110418a91c6f4e684cb77ca082ceff2e50c1267ef1f74090c0ab6f415
      (head (compile-pack-production comment-chunk (pp-production (pp-label (pp-sub (pp-def comment-chunk) left) plus-more) (pp-sub (pp-def comment-chunk) left) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment-chunk) left) body)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def comment-chunk) left)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-be359915289ac9fc1067c883901ab64509821e8f183feef76441cb8356ed0a76
      (head (compile-pack-production comment-chunk (pp-production (pp-label (pp-sub (pp-def comment-chunk) left) plus-one) (pp-sub (pp-def comment-chunk) left) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment-chunk) left) body)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-9ff62510bdf289dce0a16ada495c7fd81ae74f068c2e66d3cb08719cec798b4e
      (head (compile-pack-production comment-chunk (pp-production (pp-label (pp-sub (pp-def comment-chunk) right) seq) (pp-sub (pp-def comment-chunk) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment-chunk) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment-chunk) right) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-573ba4e5efe818892a70db89bfff90f3a943c3f05f7eeb0eae6ebffbff90271f
      (head (compile-pack-production comment-chunk (pp-production (pp-label (pp-sub (pp-sub (pp-def comment-chunk) left) body) ref) (pp-sub (pp-sub (pp-def comment-chunk) left) body) (pp-items-cons (pp-nonterminal (pp-def comment-pair-unit)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c025e7818ca1a4ab01145ba636531039547386192eb8372dde5b133c6b513220
      (head (compile-pack-production comment-chunk (pp-production (pp-label (pp-sub (pp-sub (pp-def comment-chunk) right) left) star-cons) (pp-sub (pp-sub (pp-def comment-chunk) right) left) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def comment-chunk) right) left) body)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment-chunk) right) left)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-e55c6c3a4eabbe3063bb9260840c445967935634de712756a6569ee00fd5d9b5
      (head (compile-pack-production comment-chunk (pp-production (pp-label (pp-sub (pp-sub (pp-def comment-chunk) right) left) star-empty) (pp-sub (pp-sub (pp-def comment-chunk) right) left) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-47eaf5baa224b2331282f52bc74e854bb6f1e157b482fc2fee0ebc19a90cc08a
      (head (compile-pack-production comment-chunk (pp-production (pp-label (pp-sub (pp-sub (pp-def comment-chunk) right) right) char) (pp-sub (pp-sub (pp-def comment-chunk) right) right) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-325483bbe04548892e02488481c5cc275b32b8a81d50bca0764ce36b1e0db7ea
      (head (compile-pack-production comment-chunk (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def comment-chunk) right) left) body) ref) (pp-sub (pp-sub (pp-sub (pp-def comment-chunk) right) left) body) (pp-items-cons (pp-nonterminal (pp-def comment-pair-unit)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-1443df10eaaff8ab3bb07529a9c4e72e17fc5a2699392f527480b75b983aa0b3
      (head (compile-pack-production comment-pair-unit (pp-production (pp-label (pp-def comment-pair-unit) alt-left) (pp-def comment-pair-unit) (pp-items-cons (pp-nonterminal (pp-sub (pp-def comment-pair-unit) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-1ed96e4ebe17db733ae3e2dd6841dc9e4c774e01eae0fa88a2c1f6607674f819
      (head (compile-pack-production comment-pair-unit (pp-production (pp-label (pp-def comment-pair-unit) alt-right) (pp-def comment-pair-unit) (pp-items-cons (pp-nonterminal (pp-sub (pp-def comment-pair-unit) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-46d69fcbbb4bf9d8c4f5e22f7db75e65a50ed25a0af852cc9fce6668621dc0a4
      (head (compile-pack-production comment-pair-unit (pp-production (pp-label (pp-sub (pp-def comment-pair-unit) left) class) (pp-sub (pp-def comment-pair-unit) left) (pp-items-cons (pp-terminal (pp-terminal-class comment-non-dollar)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-72a8024b73f316da3a90aa4282aabecc4f45f87efa52f1877cbcc8e41a7cb6b6
      (head (compile-pack-production comment-pair-unit (pp-production (pp-label (pp-sub (pp-def comment-pair-unit) right) seq) (pp-sub (pp-def comment-pair-unit) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment-pair-unit) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def comment-pair-unit) right) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-6ae1fd7b1ce8dd5e52adc6d29ab5ee2ae0f288c52264077eddfd8d896cdcc4b7
      (head (compile-pack-production comment-pair-unit (pp-production (pp-label (pp-sub (pp-sub (pp-def comment-pair-unit) right) left) char) (pp-sub (pp-sub (pp-def comment-pair-unit) right) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7473729d6045f3269efbdf29ce295188a7fa9481f1a5818775d5008aa978c528
      (head (compile-pack-production comment-pair-unit (pp-production (pp-label (pp-sub (pp-sub (pp-def comment-pair-unit) right) right) class) (pp-sub (pp-sub (pp-def comment-pair-unit) right) right) (pp-items-cons (pp-terminal (pp-terminal-class comment-dollar-follow)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fcc4aadef65e75a799a51a20152eec765467a34c517ba580f404196e6635fd8d
      (head (compile-pack-production compressed-token (pp-production (pp-label (pp-def compressed-token) left) (pp-def compressed-token) (pp-items-cons (pp-nonterminal (pp-sub (pp-def compressed-token) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def compressed-token) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a15f37c2af3e413cc8c2a9bfc2a1f8d60775c9575826ca38b79d049b068df53a
      (head (compile-pack-production compressed-token (pp-production (pp-label (pp-sub (pp-def compressed-token) left) ref) (pp-sub (pp-def compressed-token) left) (pp-items-cons (pp-nonterminal (pp-def compressed-token-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-396bfb9ba5a8db5d6973b45382b71601e2574b997f5de07257f68c83764c5be8
      (head (compile-pack-production compressed-token (pp-production (pp-label (pp-sub (pp-def compressed-token) right) ref) (pp-sub (pp-def compressed-token) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-bed81ffa63ba1d8e413ca6b086518aa3828222e52a62639b30aff494ef9bd9d9
      (head (compile-pack-production compressed-token-lexeme (pp-production (pp-label (pp-def compressed-token-lexeme) left) (pp-def compressed-token-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def compressed-token-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def compressed-token-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-48eb38ffe55330e23545332bb309f6c80f99c1f3a6792cdc7a8489d303e088fe
      (head (compile-pack-production compressed-token-lexeme (pp-production (pp-label (pp-sub (pp-def compressed-token-lexeme) left) node) (pp-sub (pp-def compressed-token-lexeme) left) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const compressed-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-0a4c1fab88cf90cb4641ffcf74af1adb19ca5113c3012c972415f10f8926a26a
      (head (compile-pack-production compressed-token-lexeme (pp-production (pp-label (pp-sub (pp-def compressed-token-lexeme) right) ref) (pp-sub (pp-def compressed-token-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a0bcea243ad7c313fff665f61f39c7603fa688e971a7d166e008b2632d8f0f98
      (head (compile-pack-production compressed-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) plus-more) (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) body)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-b8db193031456ea744b8b68f6fb18f5d9d746d726936a45950976441794b1e5b
      (head (compile-pack-production compressed-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) plus-one) (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) body)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-ddf33e8113a0ccca8ca3670d49716e6d651f3a10f38845c58b3db5e5d8940368
      (head (compile-pack-production compressed-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) body) class) (pp-sub (pp-sub (pp-sub (pp-def compressed-token-lexeme) left) body) body) (pp-items-cons (pp-terminal (pp-terminal-class compressed-char)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-5f63320fef9ec1b3192865d92ed036c6abaeab17bfe3f516bdcec00d2e19c4f8
      (head (compile-pack-production database (pp-production (pp-label (pp-def database) node) (pp-def database) (pp-items-cons (pp-nonterminal (pp-sub (pp-def database) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const database) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-1bbd8f2961cbccac032f3301898d7708715853ae1ce63b68cdc0a3a1e13fc407
      (head (compile-pack-production database (pp-production (pp-label (pp-sub (pp-def database) body) right) (pp-sub (pp-def database) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def database) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def database) body) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-6f0dc8ec0fb85003bbf24a4195e6d643db6a1a718aa35091b7074b59f75aec71
      (head (compile-pack-production database (pp-production (pp-label (pp-sub (pp-sub (pp-def database) body) left) ref) (pp-sub (pp-sub (pp-def database) body) left) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fe2f3a9d75caa3c6344bd8d51a891bd01c5b9f4f37e229baf3548b0e487ac855
      (head (compile-pack-production database (pp-production (pp-label (pp-sub (pp-sub (pp-def database) body) right) left) (pp-sub (pp-sub (pp-def database) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def database) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def database) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b53ff2ce4980a68bddc0997012f74b1cb759a0fb52a70f68c9d5122c2815df41
      (head (compile-pack-production database (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def database) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def database) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-outer_database)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a2e43a02b4bea1561141077f0b661ee64e005e3424d5711911378976693cf72b
      (head (compile-pack-production database (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def database) body) right) right) eof) (pp-sub (pp-sub (pp-sub (pp-def database) body) right) right) (pp-items-cons (pp-terminal pp-terminal-eof) pp-items-nil) (pa-const eof))))
      (body))
    (rule parser-pack-fact-c4c34747d68e73da2ac4318e5db999b9e8b01f0de4fb9147c62bf4d70cdc76ae
      (head (compile-pack-production include-token (pp-production (pp-label (pp-def include-token) left) (pp-def include-token) (pp-items-cons (pp-nonterminal (pp-sub (pp-def include-token) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def include-token) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-331a66bff433852d53c9cfc441a7bbde1805e16b1bd18134582c790620211355
      (head (compile-pack-production include-token (pp-production (pp-label (pp-sub (pp-def include-token) left) ref) (pp-sub (pp-def include-token) left) (pp-items-cons (pp-nonterminal (pp-def include-token-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-da52aef13982fbeb1e6fd9e5fc7b43e4e20eb768a9f924e190bd5bc0f17d7ece
      (head (compile-pack-production include-token (pp-production (pp-label (pp-sub (pp-def include-token) right) ref) (pp-sub (pp-def include-token) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-4b68c97b44e885a8ead9b45d14babc243a0eab32d93bc8bc276402fe3366f3f3
      (head (compile-pack-production include-token-lexeme (pp-production (pp-label (pp-def include-token-lexeme) left) (pp-def include-token-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def include-token-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def include-token-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-be98d9b00e7ee3714b072c874b9ba490a0fc48f42f050feadad4cb47be9c050e
      (head (compile-pack-production include-token-lexeme (pp-production (pp-label (pp-sub (pp-def include-token-lexeme) left) node) (pp-sub (pp-def include-token-lexeme) left) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def include-token-lexeme) left) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const include-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-02abcb96b83c693e2f01c228469a87adf3380dc4ccd158ee66a924b92872a343
      (head (compile-pack-production include-token-lexeme (pp-production (pp-label (pp-sub (pp-def include-token-lexeme) right) ref) (pp-sub (pp-def include-token-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-27c37b42c2eb831bef0f5df6a1f94fef147065f2bdb14ad73a763db668caf841
      (head (compile-pack-production include-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) plus-more) (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) body)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def include-token-lexeme) left) body)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-a8157ad691053615210df4e0cc8cbf8ec92dca85b5767ad9805df0a93af75dfb
      (head (compile-pack-production include-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) plus-one) (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) body)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-a0ef11fe414131a3a5670edc36823737b88a3c95bcce309635ea8c2a4f67145e
      (head (compile-pack-production include-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) body) class) (pp-sub (pp-sub (pp-sub (pp-def include-token-lexeme) left) body) body) (pp-items-cons (pp-terminal (pp-terminal-class math-symbol-char)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e948e40b8542395bcbe2acd89765ee126d4ba56f8da6c6e4955a163433d64f34
      (head (compile-pack-production kw-axiom (pp-production (pp-label (pp-def kw-axiom) left) (pp-def kw-axiom) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-axiom) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-axiom) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-85fbe141faec6ae003a07ffc15328235a0ffd5ebf2ff6933e671c8cf708cc40d
      (head (compile-pack-production kw-axiom (pp-production (pp-label (pp-sub (pp-def kw-axiom) left) ref) (pp-sub (pp-def kw-axiom) left) (pp-items-cons (pp-nonterminal (pp-def kw-axiom-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b2131c699efc63cb1e567c94c86d1e8419979bb6233fb4e5de701624785d074a
      (head (compile-pack-production kw-axiom (pp-production (pp-label (pp-sub (pp-def kw-axiom) right) ref) (pp-sub (pp-def kw-axiom) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-20bb941cb577f7dee83a4211955387abddbc65466e69cf80a87f9a05d492d9a7
      (head (compile-pack-production kw-axiom-lexeme (pp-production (pp-label (pp-def kw-axiom-lexeme) left) (pp-def kw-axiom-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-axiom-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-axiom-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b15f4fce09a5dcc8ae27b7fa59cbece30c95f3c84d8aa2c4b0cb05057f7268ed
      (head (compile-pack-production kw-axiom-lexeme (pp-production (pp-label (pp-sub (pp-def kw-axiom-lexeme) left) lit) (pp-sub (pp-def kw-axiom-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 97))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-9021cd7e33550af6a7b150dcdeb84f5c8f2dad09832a69470b2d95142a9bad23
      (head (compile-pack-production kw-axiom-lexeme (pp-production (pp-label (pp-sub (pp-def kw-axiom-lexeme) right) ref) (pp-sub (pp-def kw-axiom-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-bc21ca099cb37e0434cb1b46f602713bcebbc3832fa9026bb265f4ca8ec236c2
      (head (compile-pack-production kw-block-close (pp-production (pp-label (pp-def kw-block-close) left) (pp-def kw-block-close) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-block-close) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-block-close) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d2b3ad7469ed8d1d8f7e349d32d3bd62305012bf3b3102827301d75c15372d22
      (head (compile-pack-production kw-block-close (pp-production (pp-label (pp-sub (pp-def kw-block-close) left) ref) (pp-sub (pp-def kw-block-close) left) (pp-items-cons (pp-nonterminal (pp-def kw-block-close-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-be2506f347d6def7c1d20a9b02095c501cd9762288471dc1fd46dd21d929e117
      (head (compile-pack-production kw-block-close (pp-production (pp-label (pp-sub (pp-def kw-block-close) right) ref) (pp-sub (pp-def kw-block-close) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-5a9067e07fd92c5a39a2b775a73f3a59bf32e7536f9c04e28e565b6483268a10
      (head (compile-pack-production kw-block-close-lexeme (pp-production (pp-label (pp-def kw-block-close-lexeme) left) (pp-def kw-block-close-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-block-close-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-block-close-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-6c177294e7f48bb387476fc08a3d864510c60df64b2866f5dfc25463919dd10a
      (head (compile-pack-production kw-block-close-lexeme (pp-production (pp-label (pp-sub (pp-def kw-block-close-lexeme) left) lit) (pp-sub (pp-def kw-block-close-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 125))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-3714f263402113cf01e97a8b778eedaef96582a1ff6815cbd54e266a5f2791a2
      (head (compile-pack-production kw-block-close-lexeme (pp-production (pp-label (pp-sub (pp-def kw-block-close-lexeme) right) ref) (pp-sub (pp-def kw-block-close-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2f7160533690995311501eeee60f2b5cbce1a4dac68032b54a1d07d2e0f7ae01
      (head (compile-pack-production kw-block-open (pp-production (pp-label (pp-def kw-block-open) left) (pp-def kw-block-open) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-block-open) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-block-open) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-06588f9a5034026450cfd2a4905cf0e52275c03e832d9dca092d8da2c9c22670
      (head (compile-pack-production kw-block-open (pp-production (pp-label (pp-sub (pp-def kw-block-open) left) ref) (pp-sub (pp-def kw-block-open) left) (pp-items-cons (pp-nonterminal (pp-def kw-block-open-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ed070f29f37f02e8089082c738682b62456c7b8109d62aabe9c8a11da3b2b43f
      (head (compile-pack-production kw-block-open (pp-production (pp-label (pp-sub (pp-def kw-block-open) right) ref) (pp-sub (pp-def kw-block-open) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fa897417f0468f847b90b46d294bae1a19144d05d67d9066b2869d79e462c2bc
      (head (compile-pack-production kw-block-open-lexeme (pp-production (pp-label (pp-def kw-block-open-lexeme) left) (pp-def kw-block-open-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-block-open-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-block-open-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e9e90c0598ac2dae88b7a8d2fb08dc180fcc9dc5aefa3f4ff3345aa0af5a19d5
      (head (compile-pack-production kw-block-open-lexeme (pp-production (pp-label (pp-sub (pp-def kw-block-open-lexeme) left) lit) (pp-sub (pp-def kw-block-open-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 123))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-befdc53ba070a658efd82b54512383c253f3f1aac97b358ca7555172b5d32c7e
      (head (compile-pack-production kw-block-open-lexeme (pp-production (pp-label (pp-sub (pp-def kw-block-open-lexeme) right) ref) (pp-sub (pp-def kw-block-open-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d46777f09dd077f6f3df92a44b0d43e0efeb3f85b9f5247ef478f5f74d41e046
      (head (compile-pack-production kw-constant (pp-production (pp-label (pp-def kw-constant) left) (pp-def kw-constant) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-constant) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-constant) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-8dad30d9709968b16cb5e64e2c36ecdc1e9c41b436b0f5079f95cb17fa542bea
      (head (compile-pack-production kw-constant (pp-production (pp-label (pp-sub (pp-def kw-constant) left) ref) (pp-sub (pp-def kw-constant) left) (pp-items-cons (pp-nonterminal (pp-def kw-constant-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fa5db16132235f4937ca25c2e9fd007ab3c3bd745e355d2434e49c07f4aa5a85
      (head (compile-pack-production kw-constant (pp-production (pp-label (pp-sub (pp-def kw-constant) right) ref) (pp-sub (pp-def kw-constant) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2db60b2dafa52ea5e011c424e2aca5b4ea0ae5181483b08f7b929d28e5e48fe4
      (head (compile-pack-production kw-constant-lexeme (pp-production (pp-label (pp-def kw-constant-lexeme) left) (pp-def kw-constant-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-constant-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-constant-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7429bc2d67ac39856bf5451f12df4d857d00bc1e9a9292e5606a53dff00b846d
      (head (compile-pack-production kw-constant-lexeme (pp-production (pp-label (pp-sub (pp-def kw-constant-lexeme) left) lit) (pp-sub (pp-def kw-constant-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 99))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-4aa340381ce14d582ee97503f866ad9cd8d73c390016a009b4914eb8fd152d67
      (head (compile-pack-production kw-constant-lexeme (pp-production (pp-label (pp-sub (pp-def kw-constant-lexeme) right) ref) (pp-sub (pp-def kw-constant-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-5e2dec5a5e2975fd681dfb45a57d7f2a522fcc1c78c81b0c60f7a4c9511887a0
      (head (compile-pack-production kw-disjoint (pp-production (pp-label (pp-def kw-disjoint) left) (pp-def kw-disjoint) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-disjoint) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-disjoint) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-3b5cbdd11e348898f86bf50b3409dcb4f9702fbc61be4101d6f3858947d4d8e4
      (head (compile-pack-production kw-disjoint (pp-production (pp-label (pp-sub (pp-def kw-disjoint) left) ref) (pp-sub (pp-def kw-disjoint) left) (pp-items-cons (pp-nonterminal (pp-def kw-disjoint-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-64b7738e3bfc906bc1183ffbe817848ad39efc7be6a92cd77357b87d4adc028e
      (head (compile-pack-production kw-disjoint (pp-production (pp-label (pp-sub (pp-def kw-disjoint) right) ref) (pp-sub (pp-def kw-disjoint) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ce737d2eccf384f76b2626da2fca63afda5075a04351ce7debb1d9ca4221cbcb
      (head (compile-pack-production kw-disjoint-lexeme (pp-production (pp-label (pp-def kw-disjoint-lexeme) left) (pp-def kw-disjoint-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-disjoint-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-disjoint-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e2b00badc586a43dc5464d2fcf6351d00b235ff9800296205053d03f4fc2f462
      (head (compile-pack-production kw-disjoint-lexeme (pp-production (pp-label (pp-sub (pp-def kw-disjoint-lexeme) left) lit) (pp-sub (pp-def kw-disjoint-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 100))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-429910bcf58e9400af22c204aa85f0e8eb502c7c069450180aff32408a2dc2a7
      (head (compile-pack-production kw-disjoint-lexeme (pp-production (pp-label (pp-sub (pp-def kw-disjoint-lexeme) right) ref) (pp-sub (pp-def kw-disjoint-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-6ffc0696558e8500af26cddb8aa80fabd3fdffe5879f68dee603c3d89db5df4d
      (head (compile-pack-production kw-end (pp-production (pp-label (pp-def kw-end) left) (pp-def kw-end) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-end) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-end) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-96d4cfe2551dec08be8aef27c5f53cb7c7d86f4cbe357f3daa83f9a0cd525809
      (head (compile-pack-production kw-end (pp-production (pp-label (pp-sub (pp-def kw-end) left) ref) (pp-sub (pp-def kw-end) left) (pp-items-cons (pp-nonterminal (pp-def kw-end-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fe4e104734097df92f562c432b11d8aea8e084bbecccaafef1d4179c7f937f59
      (head (compile-pack-production kw-end (pp-production (pp-label (pp-sub (pp-def kw-end) right) ref) (pp-sub (pp-def kw-end) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-59e0da7fdcf66193781374a91f2debd286661059e9a4936c5f206ee87661743e
      (head (compile-pack-production kw-end-lexeme (pp-production (pp-label (pp-def kw-end-lexeme) left) (pp-def kw-end-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-end-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-end-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d79771684d4045c5030c67067ef6fda74deab74c618ea6f288129f15029b7627
      (head (compile-pack-production kw-end-lexeme (pp-production (pp-label (pp-sub (pp-def kw-end-lexeme) left) lit) (pp-sub (pp-def kw-end-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 46))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-e4be2dec2c345049b6c53c47ba50cea5e87893f2fe252f2ca55f5efef3cb8655
      (head (compile-pack-production kw-end-lexeme (pp-production (pp-label (pp-sub (pp-def kw-end-lexeme) right) ref) (pp-sub (pp-def kw-end-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-885e49030d868bdd3c6fd56766123d871c561c782ce17e2671fd48d660fa8a3b
      (head (compile-pack-production kw-essential (pp-production (pp-label (pp-def kw-essential) left) (pp-def kw-essential) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-essential) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-essential) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-85f88df6f5c2e76f91b294c640723a1aecaae9f0f18653d2474201949a0de04f
      (head (compile-pack-production kw-essential (pp-production (pp-label (pp-sub (pp-def kw-essential) left) ref) (pp-sub (pp-def kw-essential) left) (pp-items-cons (pp-nonterminal (pp-def kw-essential-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-6a8d5028e3b72b6f45e927447e8963594d4c0a205ffe5cce9e440466b4132b3a
      (head (compile-pack-production kw-essential (pp-production (pp-label (pp-sub (pp-def kw-essential) right) ref) (pp-sub (pp-def kw-essential) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2c66a0332a31c8d2d90d5e7c60d28557b8bb6b076d2c6e3767a66a8a3adf797f
      (head (compile-pack-production kw-essential-lexeme (pp-production (pp-label (pp-def kw-essential-lexeme) left) (pp-def kw-essential-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-essential-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-essential-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-42c3a3350b9d8ac11c6a1650d61abbe7b72f54997ae3b7ee857ff8e6f13a826f
      (head (compile-pack-production kw-essential-lexeme (pp-production (pp-label (pp-sub (pp-def kw-essential-lexeme) left) lit) (pp-sub (pp-def kw-essential-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 101))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-bc9713173f22cfa0e262dfc0b4e23cf22feaa456d5346e4c8926ca7643a2ff6f
      (head (compile-pack-production kw-essential-lexeme (pp-production (pp-label (pp-sub (pp-def kw-essential-lexeme) right) ref) (pp-sub (pp-def kw-essential-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-da56a795a8039ce00c036cdb5f3878bdcff4bae16054c0f58bb528d52a4361e9
      (head (compile-pack-production kw-float (pp-production (pp-label (pp-def kw-float) left) (pp-def kw-float) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-float) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-float) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-de56b7a17d677c098325ec3bfe238d491b3de0128557c615e4808d1bc7799bdd
      (head (compile-pack-production kw-float (pp-production (pp-label (pp-sub (pp-def kw-float) left) ref) (pp-sub (pp-def kw-float) left) (pp-items-cons (pp-nonterminal (pp-def kw-float-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-9065f76655f4355b7c41e7c43bca371fc3212ed86ec9ea6ed6a18af5b1aca486
      (head (compile-pack-production kw-float (pp-production (pp-label (pp-sub (pp-def kw-float) right) ref) (pp-sub (pp-def kw-float) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0d57977fe69e9158adf6d2d18f59a43b3d96f0a684bf5badc77ed2cd3b9c283c
      (head (compile-pack-production kw-float-lexeme (pp-production (pp-label (pp-def kw-float-lexeme) left) (pp-def kw-float-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-float-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-float-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-de82281c10ac08c2fab33c78eb4115ca117f8b67e7598e7e9ac34651092b84f5
      (head (compile-pack-production kw-float-lexeme (pp-production (pp-label (pp-sub (pp-def kw-float-lexeme) left) lit) (pp-sub (pp-def kw-float-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 102))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-ea2ff13cffe415e5df95be3110d979447054f4c0f934c382999e59df10e7793c
      (head (compile-pack-production kw-float-lexeme (pp-production (pp-label (pp-sub (pp-def kw-float-lexeme) right) ref) (pp-sub (pp-def kw-float-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-409fe652acde235aa37319eb52b8fdcdbdff158c11fefa794d349a5a5a29d3b4
      (head (compile-pack-production kw-include-close (pp-production (pp-label (pp-def kw-include-close) left) (pp-def kw-include-close) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-include-close) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-include-close) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-5d96d53e423c014c5c944ece77b3428d68c0fb0f5211029cec4f0faf0ffa161e
      (head (compile-pack-production kw-include-close (pp-production (pp-label (pp-sub (pp-def kw-include-close) left) ref) (pp-sub (pp-def kw-include-close) left) (pp-items-cons (pp-nonterminal (pp-def kw-include-close-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c67e247c5038a1d0116c1d3915be303bc7e7e1188a089cb3f3e58bb5c23d7aca
      (head (compile-pack-production kw-include-close (pp-production (pp-label (pp-sub (pp-def kw-include-close) right) ref) (pp-sub (pp-def kw-include-close) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-3f09e302a803bb9b3ade4e079284e443e0a3d566f44608c5fca3f949599d7026
      (head (compile-pack-production kw-include-close-lexeme (pp-production (pp-label (pp-def kw-include-close-lexeme) left) (pp-def kw-include-close-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-include-close-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-include-close-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7e5b573bb74740296b174633e53372c19810f5c7045274aca810928de429ae61
      (head (compile-pack-production kw-include-close-lexeme (pp-production (pp-label (pp-sub (pp-def kw-include-close-lexeme) left) lit) (pp-sub (pp-def kw-include-close-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 93))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-040c89de3c36bd044cb54abe1a5ea140a70acddb513af7f56ebd193f65fd4167
      (head (compile-pack-production kw-include-close-lexeme (pp-production (pp-label (pp-sub (pp-def kw-include-close-lexeme) right) ref) (pp-sub (pp-def kw-include-close-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7f4524da5bce2613a1e591aafacd790e54a124a934a8e3daab3e100e2b50b80c
      (head (compile-pack-production kw-include-open (pp-production (pp-label (pp-def kw-include-open) left) (pp-def kw-include-open) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-include-open) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-include-open) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-022d34b550be1a8bbd7d4acad874b540a9455f8d75e733c1371ff91b4f111fcb
      (head (compile-pack-production kw-include-open (pp-production (pp-label (pp-sub (pp-def kw-include-open) left) ref) (pp-sub (pp-def kw-include-open) left) (pp-items-cons (pp-nonterminal (pp-def kw-include-open-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-907124181d1eae877ddec9f2cf5c8d14b1df87bfb3fab64faee403482c2f97dd
      (head (compile-pack-production kw-include-open (pp-production (pp-label (pp-sub (pp-def kw-include-open) right) ref) (pp-sub (pp-def kw-include-open) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-6e772b5c67d9080e75bdf43c52cd50313c5527fec6b628c73d76128d62cdb0b0
      (head (compile-pack-production kw-include-open-lexeme (pp-production (pp-label (pp-def kw-include-open-lexeme) left) (pp-def kw-include-open-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-include-open-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-include-open-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-46d7732e50e79d70a3e004d2eccb2b3c00249defd7c86e5149a98ce14e5a6428
      (head (compile-pack-production kw-include-open-lexeme (pp-production (pp-label (pp-sub (pp-def kw-include-open-lexeme) left) lit) (pp-sub (pp-def kw-include-open-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 91))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-c31c81c9b3dda291f06fd3259e701c16c713138e3274b7d0595c0423c9be47c7
      (head (compile-pack-production kw-include-open-lexeme (pp-production (pp-label (pp-sub (pp-def kw-include-open-lexeme) right) ref) (pp-sub (pp-def kw-include-open-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-63ae57da45dcee28321c26b118ea20b40b2eb5a9aee0d8ae4506c436c326360e
      (head (compile-pack-production kw-proof (pp-production (pp-label (pp-def kw-proof) left) (pp-def kw-proof) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-proof) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-proof) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-64fd44fcb95c1593554abf38018308ecf8dc6ef3cc3046a9afdafc59adfd93d9
      (head (compile-pack-production kw-proof (pp-production (pp-label (pp-sub (pp-def kw-proof) left) ref) (pp-sub (pp-def kw-proof) left) (pp-items-cons (pp-nonterminal (pp-def kw-proof-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b56e243e6cade91718206986ed99675af5ecdd4708c28272b510df91987424dc
      (head (compile-pack-production kw-proof (pp-production (pp-label (pp-sub (pp-def kw-proof) right) ref) (pp-sub (pp-def kw-proof) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-004ee97b421e4f4b4ea27270486724fc6852fa9c9e1b9c2fe834874ba3b18ab9
      (head (compile-pack-production kw-proof-lexeme (pp-production (pp-label (pp-def kw-proof-lexeme) left) (pp-def kw-proof-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-proof-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-proof-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b6426c6868aa9ed8b1133b52dce8098405d1b327ad18c1dc58e366dab75cad98
      (head (compile-pack-production kw-proof-lexeme (pp-production (pp-label (pp-sub (pp-def kw-proof-lexeme) left) lit) (pp-sub (pp-def kw-proof-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 61))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-0409c02c16f5f64df9c16df4253e7ad403134fc1a2c6bc1ac779eb537643ceaf
      (head (compile-pack-production kw-proof-lexeme (pp-production (pp-label (pp-sub (pp-def kw-proof-lexeme) right) ref) (pp-sub (pp-def kw-proof-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0e98dae597f2a17f076121c39034af43bf80e3185f0d481bdb90224a7e924581
      (head (compile-pack-production kw-theorem (pp-production (pp-label (pp-def kw-theorem) left) (pp-def kw-theorem) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-theorem) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-theorem) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ca7df946db4e752cb54c6a0d2765fcbeb1f757b1cb5c942d8e44a02a79043f20
      (head (compile-pack-production kw-theorem (pp-production (pp-label (pp-sub (pp-def kw-theorem) left) ref) (pp-sub (pp-def kw-theorem) left) (pp-items-cons (pp-nonterminal (pp-def kw-theorem-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-bc3e3573d7108f08302555d72fa7ad9bf072e4089b31f2d54bed42fd4547bfd5
      (head (compile-pack-production kw-theorem (pp-production (pp-label (pp-sub (pp-def kw-theorem) right) ref) (pp-sub (pp-def kw-theorem) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0db30d232dfb5a0c789048f6fe4f712cc13bf00a489723d8aef2a9644cc5eb62
      (head (compile-pack-production kw-theorem-lexeme (pp-production (pp-label (pp-def kw-theorem-lexeme) left) (pp-def kw-theorem-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-theorem-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-theorem-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-df428abca9fa8b0ca9ce6da8b232e6d0b7d2c9c79f09397bda938f00491205cb
      (head (compile-pack-production kw-theorem-lexeme (pp-production (pp-label (pp-sub (pp-def kw-theorem-lexeme) left) lit) (pp-sub (pp-def kw-theorem-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 112))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-c4f2ebcc3a1fe57fe7c46f64bad9af6457c920971f7400404a1085e82a5ba835
      (head (compile-pack-production kw-theorem-lexeme (pp-production (pp-label (pp-sub (pp-def kw-theorem-lexeme) right) ref) (pp-sub (pp-def kw-theorem-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-62e13287ff0d117ebe74b5293f664389fa3aebcf9a3ff66c2248467e981c5d74
      (head (compile-pack-production kw-variable (pp-production (pp-label (pp-def kw-variable) left) (pp-def kw-variable) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-variable) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-variable) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-1984b3e6648e13fdaf74565cfe86c768d3db8c021926b2d827b5e332dd9e8adc
      (head (compile-pack-production kw-variable (pp-production (pp-label (pp-sub (pp-def kw-variable) left) ref) (pp-sub (pp-def kw-variable) left) (pp-items-cons (pp-nonterminal (pp-def kw-variable-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-bdc9ab635602073680b5d11ee7d015b1e4d15bb3733c6e0d4483e25ec017ea94
      (head (compile-pack-production kw-variable (pp-production (pp-label (pp-sub (pp-def kw-variable) right) ref) (pp-sub (pp-def kw-variable) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7ec8860f454abd53af976bb804f125f8d8dc33aa1794c429408c737898e3835c
      (head (compile-pack-production kw-variable-lexeme (pp-production (pp-label (pp-def kw-variable-lexeme) left) (pp-def kw-variable-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-variable-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def kw-variable-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c608dae57ea13dfe85507be1f4f8fc8653e9d79144ac1545671ab5db87669325
      (head (compile-pack-production kw-variable-lexeme (pp-production (pp-label (pp-sub (pp-def kw-variable-lexeme) left) lit) (pp-sub (pp-def kw-variable-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 36))) (pp-items-cons (pp-terminal (pp-terminal-char (cp 118))) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-apply cons (pa-cons (pa-slot (q-succ q-zero)) (pa-cons (pa-const nil) pa-nil))) pa-nil))))))
      (body))
    (rule parser-pack-fact-f758419ebcf45681c5620ef7602a2708f0dd0b77c88e40cfee019cd2bfea9b9e
      (head (compile-pack-production kw-variable-lexeme (pp-production (pp-label (pp-sub (pp-def kw-variable-lexeme) right) ref) (pp-sub (pp-def kw-variable-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c328f326e9819459acaa4c951900c7c7ad59ea4fe846a109304a31caea4c1bac
      (head (compile-pack-production label-token (pp-production (pp-label (pp-def label-token) left) (pp-def label-token) (pp-items-cons (pp-nonterminal (pp-sub (pp-def label-token) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def label-token) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-27dcb6080cc96102d25ee2801e73f7b027fbf1dee3b4bdac7d373fc49f659947
      (head (compile-pack-production label-token (pp-production (pp-label (pp-sub (pp-def label-token) left) ref) (pp-sub (pp-def label-token) left) (pp-items-cons (pp-nonterminal (pp-def label-token-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-07dedfc054f9793173fcdb8e6e05ae71af5c82ea9591e0a4fb8dfe7ded3b9e81
      (head (compile-pack-production label-token (pp-production (pp-label (pp-sub (pp-def label-token) right) ref) (pp-sub (pp-def label-token) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a298a56773b74429efff816baff09a5a7df108d9fc2105aa1d3dd335038c7412
      (head (compile-pack-production label-token-lexeme (pp-production (pp-label (pp-def label-token-lexeme) left) (pp-def label-token-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def label-token-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def label-token-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ed7e2ad7cd03fb2285cc60e6e14404c09bd69c95909fee34eda92a5e93dfba05
      (head (compile-pack-production label-token-lexeme (pp-production (pp-label (pp-sub (pp-def label-token-lexeme) left) node) (pp-sub (pp-def label-token-lexeme) left) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def label-token-lexeme) left) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const label-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-17a7c4e11c40350c9d56bde492e3fb5a70c339453276a9cc66e75f921b53e392
      (head (compile-pack-production label-token-lexeme (pp-production (pp-label (pp-sub (pp-def label-token-lexeme) right) ref) (pp-sub (pp-def label-token-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-506a7c1ac5e2071345288643d6e18f14d462b4c9db202b7f6d13a9ea00020b00
      (head (compile-pack-production label-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) plus-more) (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) body)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def label-token-lexeme) left) body)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-d62d7f23cdcf584d0cb7bb75a2a44458edbd1e9f0245d34805ba59d5e1d16988
      (head (compile-pack-production label-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) plus-one) (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) body)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-345317ed6c91ac653736a63c978511099ae5b25d5ec6090328815209642d0ec7
      (head (compile-pack-production label-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) body) class) (pp-sub (pp-sub (pp-sub (pp-def label-token-lexeme) left) body) body) (pp-items-cons (pp-terminal (pp-terminal-class label-char)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-db1b3068df07001908e83a055ef84cb9a693d20e0fe987b3a1e489658d506cc9
      (head (compile-pack-production left-paren (pp-production (pp-label (pp-def left-paren) left) (pp-def left-paren) (pp-items-cons (pp-nonterminal (pp-sub (pp-def left-paren) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def left-paren) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-50e64beb712d0e7ccfce97a0394aa5e265bc46a8db2a868eb0e892579bfd9dcd
      (head (compile-pack-production left-paren (pp-production (pp-label (pp-sub (pp-def left-paren) left) ref) (pp-sub (pp-def left-paren) left) (pp-items-cons (pp-nonterminal (pp-def left-paren-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-3e568c97167d8227049f71027393bd8dfbda8604d941420b528922f292cc1702
      (head (compile-pack-production left-paren (pp-production (pp-label (pp-sub (pp-def left-paren) right) ref) (pp-sub (pp-def left-paren) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-880c37945c89a2baf2d87a636d0543da088d49a451c519d04a9c8fde72e22504
      (head (compile-pack-production left-paren-lexeme (pp-production (pp-label (pp-def left-paren-lexeme) left) (pp-def left-paren-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def left-paren-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def left-paren-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c8157b7a8ade48b239b455c407b148b520204c65f483dc5b8d02e1c6b28d362c
      (head (compile-pack-production left-paren-lexeme (pp-production (pp-label (pp-sub (pp-def left-paren-lexeme) left) lit) (pp-sub (pp-def left-paren-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 40))) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-597dfcdbbffe9433e0dcef6b4e0f073a1470801031e2440257e83c4c1e12e501
      (head (compile-pack-production left-paren-lexeme (pp-production (pp-label (pp-sub (pp-def left-paren-lexeme) right) ref) (pp-sub (pp-def left-paren-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-95d8a55b2d635d586b83b376eef0bb78875f74f832ba29770feb8e2f958a59e9
      (head (compile-pack-production mm-category-compressed_word_list (pp-production (pp-label (pp-def mm-category-compressed_word_list) alt-left) (pp-def mm-category-compressed_word_list) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-compressed_word_list) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f8901c41bfb5834de5a3e31bc4f1929ff348def18186e9023d0459eac0c00050
      (head (compile-pack-production mm-category-compressed_word_list (pp-production (pp-label (pp-def mm-category-compressed_word_list) alt-right) (pp-def mm-category-compressed_word_list) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-compressed_word_list) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-6e5e07e8f887952a3d047da544caf67729e57d0302d6e01ae85f26bd2135dbff
      (head (compile-pack-production mm-category-compressed_word_list (pp-production (pp-label (pp-sub (pp-def mm-category-compressed_word_list) left) ref) (pp-sub (pp-def mm-category-compressed_word_list) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-compressed_one)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c502f027cce475602653bb989c7ee476940e8b09e3b35017751bb346d860145b
      (head (compile-pack-production mm-category-compressed_word_list (pp-production (pp-label (pp-sub (pp-def mm-category-compressed_word_list) right) ref) (pp-sub (pp-def mm-category-compressed_word_list) right) (pp-items-cons (pp-nonterminal (pp-def mm-production-compressed_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2b28b550d737e6e9ce575e5f36c293dae60f7ef4b7083ffb09c00c64455ccf33
      (head (compile-pack-production mm-category-database (pp-production (pp-label (pp-def mm-category-database) alt-left) (pp-def mm-category-database) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-database) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c406470984dff8553982e860e40427d29cd44e392d8ac32458b0d28a0a37ae9b
      (head (compile-pack-production mm-category-database (pp-production (pp-label (pp-def mm-category-database) alt-right) (pp-def mm-category-database) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-database) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-9630bc26874b55c9b737b44822fd2d6d6b095b476bbf3aa5688a157456856d89
      (head (compile-pack-production mm-category-database (pp-production (pp-label (pp-sub (pp-def mm-category-database) left) ref) (pp-sub (pp-def mm-category-database) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-database_empty)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-1bd2587c2fbcbd35ecab089ab65e0d9f1469d2bdd6247ba4c2030d6a76ff6732
      (head (compile-pack-production mm-category-database (pp-production (pp-label (pp-sub (pp-def mm-category-database) right) ref) (pp-sub (pp-def mm-category-database) right) (pp-items-cons (pp-nonterminal (pp-def mm-production-database_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-06f44599dc00a81312854bcb3ebf9abb4a53f4e483a48a84a05fa59ff95d7157
      (head (compile-pack-production mm-category-disjoint_symbol_list (pp-production (pp-label (pp-def mm-category-disjoint_symbol_list) alt-left) (pp-def mm-category-disjoint_symbol_list) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-disjoint_symbol_list) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a03d26ad21e02fd98edacd31d0c1ae33d2d2fd62448deb39cafbcb23681e0177
      (head (compile-pack-production mm-category-disjoint_symbol_list (pp-production (pp-label (pp-def mm-category-disjoint_symbol_list) alt-right) (pp-def mm-category-disjoint_symbol_list) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-disjoint_symbol_list) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-55ef28dabfdc3cec8bbf9bd2eb85bb5a87e4b863dfc35b11f877a8556b499e99
      (head (compile-pack-production mm-category-disjoint_symbol_list (pp-production (pp-label (pp-sub (pp-def mm-category-disjoint_symbol_list) left) ref) (pp-sub (pp-def mm-category-disjoint_symbol_list) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-disjoint_symbols_two)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f10b24f5256918f64ff9ac8b0a2963e55138386f8406bbde419bffb13fc5aeb2
      (head (compile-pack-production mm-category-disjoint_symbol_list (pp-production (pp-label (pp-sub (pp-def mm-category-disjoint_symbol_list) right) ref) (pp-sub (pp-def mm-category-disjoint_symbol_list) right) (pp-items-cons (pp-nonterminal (pp-def mm-production-disjoint_symbols_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c486ff8ee273439e95fce8068bb22990f56df83d99e2f976b9046cc161840019
      (head (compile-pack-production mm-category-outer_database (pp-production (pp-label (pp-def mm-category-outer_database) alt-left) (pp-def mm-category-outer_database) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-outer_database) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-4f0c57b7325d745d203a3f7e823bb1d9478f1690cc2f597f6c6af6ad2d941612
      (head (compile-pack-production mm-category-outer_database (pp-production (pp-label (pp-def mm-category-outer_database) alt-right) (pp-def mm-category-outer_database) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-outer_database) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f6c74a9f04fb56226832f3dd613399dc0e2a5d13e9d11fe1fa2dffca4bc992b1
      (head (compile-pack-production mm-category-outer_database (pp-production (pp-label (pp-sub (pp-def mm-category-outer_database) left) ref) (pp-sub (pp-def mm-category-outer_database) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-outer_database_empty)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-31a2d1786354728db3ae5634d486d7af8124a5300d16a18a650a489419d57bec
      (head (compile-pack-production mm-category-outer_database (pp-production (pp-label (pp-sub (pp-def mm-category-outer_database) right) ref) (pp-sub (pp-def mm-category-outer_database) right) (pp-items-cons (pp-nonterminal (pp-def mm-production-outer_database_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0770c2e9eb1040690302055ad41af8ab9361696a691376f38eaf8e3fe01cbf4a
      (head (compile-pack-production mm-category-outer_statement (pp-production (pp-label (pp-def mm-category-outer_statement) alt-left) (pp-def mm-category-outer_statement) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-outer_statement) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-33e38fcaa352536791e70379514f7fa743afab7ec1c979ad339bfda7826a76c7
      (head (compile-pack-production mm-category-outer_statement (pp-production (pp-label (pp-def mm-category-outer_statement) alt-right) (pp-def mm-category-outer_statement) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-outer_statement) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-cf16dad38e2df9b583d9690f62108411ccdc646526f50ab968947ed9553f8016
      (head (compile-pack-production mm-category-outer_statement (pp-production (pp-label (pp-sub (pp-def mm-category-outer_statement) left) ref) (pp-sub (pp-def mm-category-outer_statement) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_include)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2e741c4261a57de3b09db165a04aa4830246f86efb41018d82269fcfe68fef59
      (head (compile-pack-production mm-category-outer_statement (pp-production (pp-label (pp-sub (pp-def mm-category-outer_statement) right) ref) (pp-sub (pp-def mm-category-outer_statement) right) (pp-items-cons (pp-nonterminal (pp-def mm-production-outer_statement_inner)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-04522f651273dc38e05d57e33c0155bf783292c4b74c2c19ad1b3710a4a415cf
      (head (compile-pack-production mm-category-proof_header_list (pp-production (pp-label (pp-def mm-category-proof_header_list) alt-left) (pp-def mm-category-proof_header_list) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-proof_header_list) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-636d744b1aa41f5d3f5bde56fb73dfed2bcb4cb0ccf469a93fba20b98f4131ee
      (head (compile-pack-production mm-category-proof_header_list (pp-production (pp-label (pp-def mm-category-proof_header_list) alt-right) (pp-def mm-category-proof_header_list) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-proof_header_list) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c92cc3a9cf4a79e38e93729b87f9b0b81675d6db90993a6bc08271231100ef70
      (head (compile-pack-production mm-category-proof_header_list (pp-production (pp-label (pp-sub (pp-def mm-category-proof_header_list) left) ref) (pp-sub (pp-def mm-category-proof_header_list) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-header_empty)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b9fa2b669e7073d580adaed61b2c86692b1f253dbfa5049b46d6c48f3af4667b
      (head (compile-pack-production mm-category-proof_header_list (pp-production (pp-label (pp-sub (pp-def mm-category-proof_header_list) right) ref) (pp-sub (pp-def mm-category-proof_header_list) right) (pp-items-cons (pp-nonterminal (pp-def mm-production-header_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-6716dc22d190554738fa2c3578c787c6c7bbf96d137cad8a51a979d3e98c3b54
      (head (compile-pack-production mm-category-proof_list (pp-production (pp-label (pp-def mm-category-proof_list) alt-left) (pp-def mm-category-proof_list) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-proof_list) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-cdd9981d9fb032437902b6cc451ec2f006d831cfee01583c0a58c7db729a022a
      (head (compile-pack-production mm-category-proof_list (pp-production (pp-label (pp-def mm-category-proof_list) alt-right) (pp-def mm-category-proof_list) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-proof_list) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-5be22606abf200d19d67aa9a4d1968d06a4a839e0c011a1bed500848dee0b3c9
      (head (compile-pack-production mm-category-proof_list (pp-production (pp-label (pp-sub (pp-def mm-category-proof_list) left) ref) (pp-sub (pp-def mm-category-proof_list) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-proof_one)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7210a6295fdb4dfb7807040a48dea587803bedc8d750fa7bad9f862f6286d702
      (head (compile-pack-production mm-category-proof_list (pp-production (pp-label (pp-sub (pp-def mm-category-proof_list) right) alt-left) (pp-sub (pp-def mm-category-proof_list) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-category-proof_list) right) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c2b76c822cf1322dc464a5f5d680b1a8665507a2c431b03fd56a3be660d77fc1
      (head (compile-pack-production mm-category-proof_list (pp-production (pp-label (pp-sub (pp-def mm-category-proof_list) right) alt-right) (pp-sub (pp-def mm-category-proof_list) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ea5c5dd7ea2990586860d008014488dbad9bc2da650d3610b0bc446bf88c8f31
      (head (compile-pack-production mm-category-proof_list (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-category-proof_list) right) left) ref) (pp-sub (pp-sub (pp-def mm-category-proof_list) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-proof_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b5ac02572be9089b12353f4de92c3d4d26dd5b038f23656b0a61e56b96b1ecc3
      (head (compile-pack-production mm-category-proof_list (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) alt-left) (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-435812bbf5874fdf5c95af5ada64e7b4999b5c44f3b2b304f6bd213943d78d1c
      (head (compile-pack-production mm-category-proof_list (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) alt-right) (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-36ced28862281c0fcddb38d42ad6fec92b18e32fff4735cc6c7af4f129e32b58
      (head (compile-pack-production mm-category-proof_list (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-proof_unknown_one)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d77c3367a2e846180169d48690fb265622fc0472e6c1f969ef553ee027904463
      (head (compile-pack-production mm-category-proof_list (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) right) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-category-proof_list) right) right) right) (pp-items-cons (pp-nonterminal (pp-def mm-production-proof_unknown_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-68082a766926c991f2ef470c9acff69b73fc745fef089f7948e100f40cdb6af5
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-def mm-category-statement) alt-left) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-statement) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-240517a9c01c7b87b31e60f7e1dc736eb9ee28f9b101945d63e15c4416158fa7
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-def mm-category-statement) alt-right) (pp-def mm-category-statement) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-statement) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0bc503f60c2a8824d16ee7956710b7ff444496b1b68a49ad03d9de5c58723d43
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-def mm-category-statement) left) ref) (pp-sub (pp-def mm-category-statement) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_const)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e96677b499aaedd0d0999dfa052a11e6f72077305834ec0aa3d3a6ef9f5866e1
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-def mm-category-statement) right) alt-left) (pp-sub (pp-def mm-category-statement) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-category-statement) right) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-dcba16633af9778ffb077176b7e9470dc9bb8a34e838a87b5a396f78270661f0
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-def mm-category-statement) right) alt-right) (pp-sub (pp-def mm-category-statement) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-category-statement) right) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-df4fe0f75af9d1cc9c6248c2a46ad3ab13fc92187bb8c5d374da287c2ab33bc3
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) left) ref) (pp-sub (pp-sub (pp-def mm-category-statement) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_var)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-1c9fdc82ed44a3b4a481ef3d480a07163b6c63ce583990d9e4fe0f1ab4abb8f1
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) right) alt-left) (pp-sub (pp-sub (pp-def mm-category-statement) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e1cd9558a3ec962cf21e0ac655a064b7bd0d835807f13935a9253927ddcf1d2c
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-category-statement) right) right) alt-right) (pp-sub (pp-sub (pp-def mm-category-statement) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c51c8a188453ff4f83af5fe2099ca9925eb488fda35c5182fa338609d0eb17d5
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_disjoint)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d5d88d2120cd39746b2245d1672c2ca44d24251885cc0f1b05928a5247e80169
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) alt-left) (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-81bd9be33e2eb15bc396d4a6a8cfabb1f815a26a5090de5ff1fa6cc6edbf8719
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) alt-right) (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-8230ffde7d9ade00bd7735a86a56e89430418ec7cc33bc4244f269990a5ccd96
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_float)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-62fbc00804934748af6701da170d57a461b1eb71cdd54644399933adeff2885a
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) alt-left) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f24fa334e22af45d9cb9d88f3440afc62455582db5fea24c186f4df6b48e25fb
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) alt-right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-3fd15b94b850d8f4578b84ff0e759a5bcfc7bc55248bac8f5a06c07cf10fbeb1
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_essential)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7350c58812185c8ab62ac23d0708a0162ef4df84dec166fc42acb103ea69c0ae
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) alt-left) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-945e5235d5f2653c59a9b4547b32ead1fcb7a6d4c81b5e9813aa50ba3fffc389
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) alt-right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-371493129732a96f6bacd85873c28e28419d305c1b10ffa731aa196449da40b4
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_axiom)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2233d3ff446f57d990025fe2d5389cbba281215acff63e696ef937e11d5cca25
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) alt-left) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c501ea6b8f50aae41cac4da359023718be457e28de63e8791cccfa6ea4cd5254
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) alt-right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e4bcf161de9d9a3fc149843c1294e80d1fdab35aba926f243c3ed1f2c68a9b36
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_theorem_normal)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-5a86018217021dafa0fd6f89cffb628c11ab99c67bd2c5acb4afe574c13cc4de
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) alt-left) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-3c57f5ac77bbc2086c07cb6f65ff6fd3ee3e31df42c21ed2be0c7055f51b4ec9
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) alt-right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e0c1fea30d035132ba615c400d5848d3d73060714ebf167276287b42ad95c9ac
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_theorem_compressed)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-5426789d565b7f43a815d5c0b83d21cddc69351cbca540a860c7b7d1a06d773b
      (head (compile-pack-production mm-category-statement (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) right) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-category-statement) right) right) right) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-def mm-production-statement_block)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d0d43712528bdb4f529a934a9f0c7eb5210133e75e09ac3596cf5c895bb2cf77
      (head (compile-pack-production mm-category-symbol_list (pp-production (pp-label (pp-def mm-category-symbol_list) alt-left) (pp-def mm-category-symbol_list) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-symbol_list) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-4dbbcd97ae756ed4556d3f6cd9c3406a84b72243085778926ff32811e81abd5f
      (head (compile-pack-production mm-category-symbol_list (pp-production (pp-label (pp-def mm-category-symbol_list) alt-right) (pp-def mm-category-symbol_list) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-category-symbol_list) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e0d99bd0e997438542d5b570e2462e7b529bc61614b79a6e454aa4cb1d3656a7
      (head (compile-pack-production mm-category-symbol_list (pp-production (pp-label (pp-sub (pp-def mm-category-symbol_list) left) ref) (pp-sub (pp-def mm-category-symbol_list) left) (pp-items-cons (pp-nonterminal (pp-def mm-production-symbols_one)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a10d9b058173440260b01f442b04f3dc291468e96ec5e224ecea3a233d2ac168
      (head (compile-pack-production mm-category-symbol_list (pp-production (pp-label (pp-sub (pp-def mm-category-symbol_list) right) ref) (pp-sub (pp-def mm-category-symbol_list) right) (pp-items-cons (pp-nonterminal (pp-def mm-production-symbols_more)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-cff030870bf143a105eb03bcfa936d2e53398cedc8ed06a086d51285ca3dd76f
      (head (compile-pack-production mm-production-compressed_more (pp-production (pp-label (pp-def mm-production-compressed_more) node) (pp-def mm-production-compressed_more) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-compressed_more) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const compressed_more) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-7df54419978e42ae5a5062fa2a853276aecf07a4b181fb8968c13db7ae724a88
      (head (compile-pack-production mm-production-compressed_more (pp-production (pp-label (pp-sub (pp-def mm-production-compressed_more) body) seq) (pp-sub (pp-def mm-production-compressed_more) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-833726c0fae2877697d4ad24cab6f57c2942585d3a5d4fe7ed43da17988547f4
      (head (compile-pack-production mm-production-compressed_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) left) (pp-items-cons (pp-nonterminal (pp-def compressed-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fe5772d29f9cd3629f501d83a17a3a7fa09f1847ae7969260d686e612ea49297
      (head (compile-pack-production mm-production-compressed_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) left) (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-45f94f97b3b98c4a03b99fe82d8cb643f30083a6d3ead8f69ffbd0bfb024a27c
      (head (compile-pack-production mm-production-compressed_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-compressed_word_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0d721ee2e0de5f95155552a55b7fc128ca7d38f315aa0203bd6b24ed973e7f3a
      (head (compile-pack-production mm-production-compressed_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-def mm-production-compressed_more) body) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-9e8bd37920548e4630cda4323c82574785b5bccc5d7da2577ab52d28fc70043b
      (head (compile-pack-production mm-production-compressed_one (pp-production (pp-label (pp-def mm-production-compressed_one) node) (pp-def mm-production-compressed_one) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-compressed_one) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const compressed_one) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-c1360feee6b7a74e857fe0decc7f5e087770b5bc1c549868855778b5e28bbd0d
      (head (compile-pack-production mm-production-compressed_one (pp-production (pp-label (pp-sub (pp-def mm-production-compressed_one) body) left) (pp-sub (pp-def mm-production-compressed_one) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-compressed_one) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-compressed_one) body) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-78ef3f02d586999a32089db7c32a0a50ce2a9c049391796ebe1d38738d7418d8
      (head (compile-pack-production mm-production-compressed_one (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-compressed_one) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-compressed_one) body) left) (pp-items-cons (pp-nonterminal (pp-def compressed-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-8c12d9b171dae6f10974d958432899a51ec1f3632e6c9ac6474d5b39fffe6d4f
      (head (compile-pack-production mm-production-compressed_one (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-compressed_one) body) right) eps) (pp-sub (pp-sub (pp-def mm-production-compressed_one) body) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-4cf8ee86c7845182cb69c36181684cf2924216596cde8e8090de8b3e7d3488b1
      (head (compile-pack-production mm-production-database_empty (pp-production (pp-label (pp-def mm-production-database_empty) node) (pp-def mm-production-database_empty) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-database_empty) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const database_empty) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-6e83280b4357c53c9b8fd0fbefe3e6576711a6eb1d36a6053b40e9eab288d9b0
      (head (compile-pack-production mm-production-database_empty (pp-production (pp-label (pp-sub (pp-def mm-production-database_empty) body) eps) (pp-sub (pp-def mm-production-database_empty) body) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-252451b3f6e8c96672f3239371f9705aa3c2e2fe77d849905d4b1bdba0ffb6b1
      (head (compile-pack-production mm-production-database_more (pp-production (pp-label (pp-def mm-production-database_more) node) (pp-def mm-production-database_more) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-database_more) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const database_more) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-9f2c4be3287173eb83e722911dbe2eb09d3f6f056fc2c29d5b3cb0b59e65a5b6
      (head (compile-pack-production mm-production-database_more (pp-production (pp-label (pp-sub (pp-def mm-production-database_more) body) seq) (pp-sub (pp-def mm-production-database_more) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-database_more) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-database_more) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-3feb3dd18f4afd6a82b425cbc28371ac450a82d613d0edaa080924d131412ce1
      (head (compile-pack-production mm-production-database_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-database_more) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-database_more) body) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-statement)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-bfe2d9e97765602fa0833dcd2c51fe438cd4894a194ce4723f20953eb652c3f4
      (head (compile-pack-production mm-production-database_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) left) (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0e423f11e78aea2ae61a71ea845e95e6fab44dd2b5842f8852c9e0b306a3dae0
      (head (compile-pack-production mm-production-database_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-database)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-987da4abb9ad7de16661c74efa739b3f8545e47ab72199c06ee77379356c20da
      (head (compile-pack-production mm-production-database_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-def mm-production-database_more) body) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-f9411a99a8b9fcb71c5c02d22ceb8fc5b7e5348ec27201572fac67567e7fefac
      (head (compile-pack-production mm-production-disjoint_symbols_more (pp-production (pp-label (pp-def mm-production-disjoint_symbols_more) node) (pp-def mm-production-disjoint_symbols_more) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-disjoint_symbols_more) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const disjoint_symbols_more) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-f4778b98f07308526c78dbacc54bbecfb16f087ae281810ace93adcaaf8c4339
      (head (compile-pack-production mm-production-disjoint_symbols_more (pp-production (pp-label (pp-sub (pp-def mm-production-disjoint_symbols_more) body) seq) (pp-sub (pp-def mm-production-disjoint_symbols_more) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-5d015f85442395c38869f330e1cbbd11ca5143e7118b82299c45f4d23f1e08e3
      (head (compile-pack-production mm-production-disjoint_symbols_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) left) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a92b4d7eab1cf87fa3ce071ed80539fc5d443acbc4719dd8434c40150a8ab481
      (head (compile-pack-production mm-production-disjoint_symbols_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) left) (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c4d813150323742a0ac8c05a1589e92be3bdd627a6768b922e56c214b1d22e70
      (head (compile-pack-production mm-production-disjoint_symbols_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-disjoint_symbol_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f3a3964906bb2773b286fe039df2a28123a0e6dedd0c0f8c0c5e0080c05bf45d
      (head (compile-pack-production mm-production-disjoint_symbols_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_more) body) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-f1da58ce8f4253205b99c48ddd87592cefaa60951b21ec08127281df2b9387ba
      (head (compile-pack-production mm-production-disjoint_symbols_two (pp-production (pp-label (pp-def mm-production-disjoint_symbols_two) node) (pp-def mm-production-disjoint_symbols_two) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-disjoint_symbols_two) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const disjoint_symbols_two) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-43b6be76b15d450e6dccd20af597d1ea9b083c890489bd92d6ebb663442dd9fc
      (head (compile-pack-production mm-production-disjoint_symbols_two (pp-production (pp-label (pp-sub (pp-def mm-production-disjoint_symbols_two) body) seq) (pp-sub (pp-def mm-production-disjoint_symbols_two) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-0422ec6c9f9564f9035a78ceb42f3398fd2d979ce0f38bc38eeb8ceb271979f9
      (head (compile-pack-production mm-production-disjoint_symbols_two (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) left) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-8af93f7fbc61ad6d392f2187193b108adb2d0101670fa6dd1774102553424a2f
      (head (compile-pack-production mm-production-disjoint_symbols_two (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) left) (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fe8a90350779a8cc3f6a27672a2013ebb2fb8e009c9fa53bc0f9eccf61276225
      (head (compile-pack-production mm-production-disjoint_symbols_two (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) left) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-95a35b8429667880a1aeadc2fc659e00ee399e44ba8906d18322680d5fdcbc24
      (head (compile-pack-production mm-production-disjoint_symbols_two (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-def mm-production-disjoint_symbols_two) body) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-d6b139dbaa324c3320074d7e8887b2b930c2a66c8df9d269fe4fbd58409f565b
      (head (compile-pack-production mm-production-header_empty (pp-production (pp-label (pp-def mm-production-header_empty) node) (pp-def mm-production-header_empty) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-header_empty) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const header_empty) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-3edb520b7de6cea74f2babf92d4664acb48f0818c30dc874a00395f278337ddd
      (head (compile-pack-production mm-production-header_empty (pp-production (pp-label (pp-sub (pp-def mm-production-header_empty) body) eps) (pp-sub (pp-def mm-production-header_empty) body) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-777860a4233b1bb7ac05061f10783b53d096c621c8a14258a5a1de49763671fe
      (head (compile-pack-production mm-production-header_more (pp-production (pp-label (pp-def mm-production-header_more) node) (pp-def mm-production-header_more) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-header_more) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const header_more) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-b303545bdb88866b52d6f61cac84a9c6458b00783eebce9baba972abe9f4e297
      (head (compile-pack-production mm-production-header_more (pp-production (pp-label (pp-sub (pp-def mm-production-header_more) body) seq) (pp-sub (pp-def mm-production-header_more) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-header_more) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-header_more) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-8937daebaef63609647d707cd7ebb70f65c4bea9a826648258f4547df001d1cc
      (head (compile-pack-production mm-production-header_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-header_more) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-header_more) body) left) (pp-items-cons (pp-nonterminal (pp-def proof-label-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a25d1b36824000e6706628498dd449c8876a349ff9c6c1b9d10b50cf6d941e1d
      (head (compile-pack-production mm-production-header_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) left) (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-aaff2118f29f99d57e789b0b02c7b806374022080009e954a9fbf940d5fe0fae
      (head (compile-pack-production mm-production-header_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-proof_header_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-71027fdb94c9bc4c938fd7224c4ea98250cdb1cf43c1bd1d66dc06f9711bcc03
      (head (compile-pack-production mm-production-header_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-def mm-production-header_more) body) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-642fccc15dcb107556768429db06cfedcf3c47a48f0673d7a312a5c91b6f54d9
      (head (compile-pack-production mm-production-outer_database_empty (pp-production (pp-label (pp-def mm-production-outer_database_empty) node) (pp-def mm-production-outer_database_empty) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-outer_database_empty) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const outer_database_empty) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-f5b4686c8d29c9be9ab57fc761b30752e2a5c53530bdeeec78a661bc1651486c
      (head (compile-pack-production mm-production-outer_database_empty (pp-production (pp-label (pp-sub (pp-def mm-production-outer_database_empty) body) eps) (pp-sub (pp-def mm-production-outer_database_empty) body) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-9e6936bf507c08265d89674aa46b0489ef5ffed13106893b7e08c46ba0bc2b39
      (head (compile-pack-production mm-production-outer_database_more (pp-production (pp-label (pp-def mm-production-outer_database_more) node) (pp-def mm-production-outer_database_more) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-outer_database_more) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const outer_database_more) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-682625747a36b0f99baba0bc1350ca64f0e06ed6e7fff9cd07f9749fea97277a
      (head (compile-pack-production mm-production-outer_database_more (pp-production (pp-label (pp-sub (pp-def mm-production-outer_database_more) body) seq) (pp-sub (pp-def mm-production-outer_database_more) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-85512e7caa10e75b040d994be9b3d63df01c938cb8ae29d4a3f13285a2132ddd
      (head (compile-pack-production mm-production-outer_database_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-outer_statement)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a9d30f633937392c6f359e081badb8538bee9ca617d899959b6fd381923ec5c0
      (head (compile-pack-production mm-production-outer_database_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) left) (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-bab5d072062b08ad8326ac671d042bcba394aa33fcc827aa0c963e2f9e30e53a
      (head (compile-pack-production mm-production-outer_database_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-outer_database)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ce93b0634d5ec71b2fae367c792a62deb9d5ed3670cc6c6945f68d269c1d9ed9
      (head (compile-pack-production mm-production-outer_database_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-def mm-production-outer_database_more) body) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-6ea7b5a1b5e561b93779b35f2a182260a2a5f8a1e0d20a5c6e069ee16ee4e61b
      (head (compile-pack-production mm-production-outer_statement_inner (pp-production (pp-label (pp-def mm-production-outer_statement_inner) node) (pp-def mm-production-outer_statement_inner) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-outer_statement_inner) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const outer_statement_inner) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-f9f5b37651192fa6d98ebb505cd3536336f07752c49f1ca84c2fbf732bfe040b
      (head (compile-pack-production mm-production-outer_statement_inner (pp-production (pp-label (pp-sub (pp-def mm-production-outer_statement_inner) body) left) (pp-sub (pp-def mm-production-outer_statement_inner) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-outer_statement_inner) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-outer_statement_inner) body) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-9a674307bbf8f244c4788f387b637bcc9dcc6444291e5a1086810ba5ef06c0fb
      (head (compile-pack-production mm-production-outer_statement_inner (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-outer_statement_inner) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-outer_statement_inner) body) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-statement)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-da3ac7150858a7401ea065ab2977c2c5d90efc5a0aa431c3b17d1bad658c3720
      (head (compile-pack-production mm-production-outer_statement_inner (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-outer_statement_inner) body) right) eps) (pp-sub (pp-sub (pp-def mm-production-outer_statement_inner) body) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-1dda15442838d5212e7b2b1774cd6576e965e5185bc3abbc8541316e81065c81
      (head (compile-pack-production mm-production-proof_more (pp-production (pp-label (pp-def mm-production-proof_more) node) (pp-def mm-production-proof_more) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-proof_more) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const proof_more) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-d15b10367aa2ee44d2c1bac98ed3e9e5eabfc5582327bb6a0b46f82e4a31eab1
      (head (compile-pack-production mm-production-proof_more (pp-production (pp-label (pp-sub (pp-def mm-production-proof_more) body) seq) (pp-sub (pp-def mm-production-proof_more) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-proof_more) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-25bafd357db58364b1ee3673a679b6ede747eb3c2d84dbc371fc86faf06dacf5
      (head (compile-pack-production mm-production-proof_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_more) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-proof_more) body) left) (pp-items-cons (pp-nonterminal (pp-def proof-label-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f670eaf6bdd2419d425d713d58fbd67a4dc847b69f01f231e73f19ada6be1d18
      (head (compile-pack-production mm-production-proof_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) left) (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-20fc82879dbf6d90cd0f2d3385389c7417924bc5ea20355df129e80f44cd92f6
      (head (compile-pack-production mm-production-proof_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-proof_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f1385533bde0be9a2d3ddd848dd90bb3645647d2d95d43437fcb979fed588102
      (head (compile-pack-production mm-production-proof_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_more) body) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-69f88e65550b45f14952392591cb61ac47eaec918908819c7f28565da0909bf0
      (head (compile-pack-production mm-production-proof_one (pp-production (pp-label (pp-def mm-production-proof_one) node) (pp-def mm-production-proof_one) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-proof_one) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const proof_one) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-8bec52621b85e2359aeaf6f90ad8c948d835bfa30d02c328bf971b695d755f38
      (head (compile-pack-production mm-production-proof_one (pp-production (pp-label (pp-sub (pp-def mm-production-proof_one) body) left) (pp-sub (pp-def mm-production-proof_one) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-proof_one) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-proof_one) body) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-406d54a200155099d3c3919ef7088410deba6c721c496f4ebb270496211a3e69
      (head (compile-pack-production mm-production-proof_one (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_one) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-proof_one) body) left) (pp-items-cons (pp-nonterminal (pp-def proof-label-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-42c1106f0987a8a099bd6e1b66f56d4f981620de1e0a32097c198e0d0cc8a0eb
      (head (compile-pack-production mm-production-proof_one (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_one) body) right) eps) (pp-sub (pp-sub (pp-def mm-production-proof_one) body) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-ba2cf78373f3e8d52731a21fd648ccad1de8103473405eeb6dd9187a77ca9d75
      (head (compile-pack-production mm-production-proof_unknown_more (pp-production (pp-label (pp-def mm-production-proof_unknown_more) node) (pp-def mm-production-proof_unknown_more) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-proof_unknown_more) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const proof_unknown_more) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-4c7ffbad8d4aa31596dec2606b971c0f124af3916400a8e63b5f9c1f0b6d075f
      (head (compile-pack-production mm-production-proof_unknown_more (pp-production (pp-label (pp-sub (pp-def mm-production-proof_unknown_more) body) right) (pp-sub (pp-def mm-production-proof_unknown_more) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-92ac5a90d1a59d657a6dc6284cc817a39bd4f843d9f8411fd407d5553de9fff7
      (head (compile-pack-production mm-production-proof_unknown_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) left) (pp-items-cons (pp-nonterminal (pp-def unknown-proof)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-98b947ae39c6fe88c73676abc7d6b8d4bc5b443997ec0f76ca48a2aafbf3f10f
      (head (compile-pack-production mm-production-proof_unknown_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) left) (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-67a2c6f68b48691225caf2e9322cf0c07d9c910de2ef6e184a71e747c40d57f8
      (head (compile-pack-production mm-production-proof_unknown_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-proof_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d0ef3b0c5efc97feeea4aab68184eb5c436749123a1064052a566eb33f403bac
      (head (compile-pack-production mm-production-proof_unknown_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-def mm-production-proof_unknown_more) body) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-fadc664699093ebac80b7b706b5fdc3cbee06f1d18d599128648662909d4f49f
      (head (compile-pack-production mm-production-proof_unknown_one (pp-production (pp-label (pp-def mm-production-proof_unknown_one) node) (pp-def mm-production-proof_unknown_one) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-proof_unknown_one) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const proof_unknown_one) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-4477ef0e18d90067261708808633bb8293d37568db766b58d52474987b0a8854
      (head (compile-pack-production mm-production-proof_unknown_one (pp-production (pp-label (pp-sub (pp-def mm-production-proof_unknown_one) body) right) (pp-sub (pp-def mm-production-proof_unknown_one) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-proof_unknown_one) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-proof_unknown_one) body) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-5a46e9786da4f101ac6075e654654dd8c5d214f9c3dfc5e9133b3f5232347733
      (head (compile-pack-production mm-production-proof_unknown_one (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_unknown_one) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-proof_unknown_one) body) left) (pp-items-cons (pp-nonterminal (pp-def unknown-proof)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-54506cbc9414d8ff761ed8038d3d804b920759410cbdbb7627ed0b010ca7fb18
      (head (compile-pack-production mm-production-proof_unknown_one (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-proof_unknown_one) body) right) eps) (pp-sub (pp-sub (pp-def mm-production-proof_unknown_one) body) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-f6a85e2c62b8ae63b1014839a549abbc62e75526193cad5f48b861369f159216
      (head (compile-pack-production mm-production-statement_axiom (pp-production (pp-label (pp-def mm-production-statement_axiom) node) (pp-def mm-production-statement_axiom) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-statement_axiom) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const statement_axiom) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-9c0ccf919368ee02126f0ae7fbfda7b650dc88159d77c4fea7f6e05c12f4ebc3
      (head (compile-pack-production mm-production-statement_axiom (pp-production (pp-label (pp-sub (pp-def mm-production-statement_axiom) body) seq) (pp-sub (pp-def mm-production-statement_axiom) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-8d0c19173434e4c35d89f30b6e62678290b6db9d52204917fcf12c03bfadd68a
      (head (compile-pack-production mm-production-statement_axiom (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) left) (pp-items-cons (pp-nonterminal (pp-def label-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e665975c4f22b7cbd41d1a52bf978543936f357ef0cc3ec64044a875e7450085
      (head (compile-pack-production mm-production-statement_axiom (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-fbfe4002dd3ccf49970df192bc60967b64b07141bb23070fc924d02693ba7d27
      (head (compile-pack-production mm-production-statement_axiom (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-axiom)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7d40ae4b9698c2b6329823f47381068f13026850ac3435b9f30f27bbbd5ce898
      (head (compile-pack-production mm-production-statement_axiom (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) left) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2800dcddb71489a45ad700325cc24d4a807aa4ff615fd75310fee75ff91f610e
      (head (compile-pack-production mm-production-statement_axiom (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-76fbcea04ce2cd06fe26ddda5a134c6148810698e4b2d9394ec46ec92555ddc4
      (head (compile-pack-production mm-production-statement_axiom (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-59f0243eebe9214f902c63851fa2a10daa5b7a40af5efa3c6b6a72ed967508be
      (head (compile-pack-production mm-production-statement_axiom (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e848c076d1408304f40f9b2d799236169fc879cff4253b18eda6fb3b3f43a57d
      (head (compile-pack-production mm-production-statement_axiom (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_axiom) body) right) right) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-8bce5173139e5c64727b458260bd4d3e798aeaca209df983abfd0057018f4f47
      (head (compile-pack-production mm-production-statement_block (pp-production (pp-label (pp-def mm-production-statement_block) node) (pp-def mm-production-statement_block) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-statement_block) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const statement_block) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-be95d6b6bf1247a2c63dada1cf480e4da70b614502c16e3f02cd9a3f53425103
      (head (compile-pack-production mm-production-statement_block (pp-production (pp-label (pp-sub (pp-def mm-production-statement_block) body) right) (pp-sub (pp-def mm-production-statement_block) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_block) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-4089986d58115a2c095971b2313960e1ee6f0b61c8688858d20940113ff1b0d9
      (head (compile-pack-production mm-production-statement_block (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_block) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-statement_block) body) left) (pp-items-cons (pp-nonterminal (pp-def kw-block-open)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b75d31a888661d6e7b1118c4c10b152e5a70778b554c4a9c728c9209b144c151
      (head (compile-pack-production mm-production-statement_block (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) left) (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b0f4a27838be801a0abc6c4a9edf954e769a6a4ccccdd4f568fba1cc6bfd6653
      (head (compile-pack-production mm-production-statement_block (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-database)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a8e9a54443b6070c88d84b68fa1bf304cbc525bec91ed124b233a27fa22bced9
      (head (compile-pack-production mm-production-statement_block (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) right) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-a8292de4c76f8851357b8c49947d7f95f9c05eee017db1c10699600e72341355
      (head (compile-pack-production mm-production-statement_block (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-block-close)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-62ad374ea85e0dc9054cf4d7bf6665da3b2676ec366c0a1ce9cff000f509ff9d
      (head (compile-pack-production mm-production-statement_block (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_block) body) right) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-e8f58c7476657dddf5bf899de6f9e917b153cadcabd13c04c05d38ee7da63ada
      (head (compile-pack-production mm-production-statement_const (pp-production (pp-label (pp-def mm-production-statement_const) node) (pp-def mm-production-statement_const) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-statement_const) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const statement_const) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-126022c742339858940906a45ad0fc36ec19c366b1a9cb5846fc04bc89167e95
      (head (compile-pack-production mm-production-statement_const (pp-production (pp-label (pp-sub (pp-def mm-production-statement_const) body) right) (pp-sub (pp-def mm-production-statement_const) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_const) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-586951cbe4de19fb302f836c90c6e1d0ec9f48f6311e8b255f3d0454628cbd16
      (head (compile-pack-production mm-production-statement_const (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_const) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-statement_const) body) left) (pp-items-cons (pp-nonterminal (pp-def kw-constant)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-875c9a4b626e9693bdc94ad03c27af563041a0529575ddd97fccc63902d216c7
      (head (compile-pack-production mm-production-statement_const (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) left) (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d6264ccd24becfc15be196d67d93cd88d05868e8a0bd22a19f5631328d5f13a2
      (head (compile-pack-production mm-production-statement_const (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-fcc858e111fc0a550fcc29d7e6b47787e693a3c2d3865f679cccc5fb0ae5f144
      (head (compile-pack-production mm-production-statement_const (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) right) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-71a6cea932bb07a3e5723ccc2d559fc10bb7f89ea5875626f590bc0190a52026
      (head (compile-pack-production mm-production-statement_const (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-3d811cdc0aed107995f3812ac2ef3a7b8e67a6d9ce0a7a7e4ff7cbdf5030f194
      (head (compile-pack-production mm-production-statement_const (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_const) body) right) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-de094b58d870c9925b6c426bcdb6814577b6ab32352eccada8779a2dc23c5864
      (head (compile-pack-production mm-production-statement_disjoint (pp-production (pp-label (pp-def mm-production-statement_disjoint) node) (pp-def mm-production-statement_disjoint) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-statement_disjoint) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const statement_disjoint) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-318767c5fe686a98c9fcf7a8773b93e7b2deb546a8c31c6f484a719412b68f64
      (head (compile-pack-production mm-production-statement_disjoint (pp-production (pp-label (pp-sub (pp-def mm-production-statement_disjoint) body) right) (pp-sub (pp-def mm-production-statement_disjoint) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-1a87d7f99b836a05cc4698767b47da6860a087ce967475946962e21b79c08630
      (head (compile-pack-production mm-production-statement_disjoint (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) left) (pp-items-cons (pp-nonterminal (pp-def kw-disjoint)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2f2da030ec04dc20906ab38511f54709d090989e877c998cf138889d2f3f7063
      (head (compile-pack-production mm-production-statement_disjoint (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) left) (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-16f622baddec655036b8d50d000a80a9f8ddb13b6a1221edd21b8c78d3c95ed4
      (head (compile-pack-production mm-production-statement_disjoint (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-disjoint_symbol_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0f96da8fa18187791686376fb41b13e9e12ce3cbbb1ffefee8cd7b7c2c9dc316
      (head (compile-pack-production mm-production-statement_disjoint (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) right) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-040db6e381f55f4186dad96a20d50a7ca90e8c52c2f4774ab6c118643b21683e
      (head (compile-pack-production mm-production-statement_disjoint (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-73fe63983fdec5941ff49d005ef9053bfb4a4f68a9939a6fade5d0b9dfcde55e
      (head (compile-pack-production mm-production-statement_disjoint (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_disjoint) body) right) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-0ccb72433899c9494a6a792c2a6fbd3d3fef0149229cec17e69bbb199b47aa81
      (head (compile-pack-production mm-production-statement_essential (pp-production (pp-label (pp-def mm-production-statement_essential) node) (pp-def mm-production-statement_essential) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-statement_essential) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const statement_essential) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-93ff297cadc5aba3618f59fbef724057db24a5c824506155623648bd9c82ab4e
      (head (compile-pack-production mm-production-statement_essential (pp-production (pp-label (pp-sub (pp-def mm-production-statement_essential) body) seq) (pp-sub (pp-def mm-production-statement_essential) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-c41505ef43304db0c48f955cbce034576c50abc585a974b959f00a9df1123068
      (head (compile-pack-production mm-production-statement_essential (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) left) (pp-items-cons (pp-nonterminal (pp-def label-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-4d05fe2c19cb7b80c2e35c032a5128a80e6ef944836eff871c870ffcb51f950d
      (head (compile-pack-production mm-production-statement_essential (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-ff993bc3ef6f284d91820ccc003f1d738447b51f70cf0715ebc34629e9fc4065
      (head (compile-pack-production mm-production-statement_essential (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-essential)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-0166bc3551a5ed2866d912628c3169ed42355bb37a68bb22c24cad8dfd893138
      (head (compile-pack-production mm-production-statement_essential (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) left) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a088c60590876e8e04a6a2ddf5b216b9ac6223f5f2ba003fa6306e9aca463604
      (head (compile-pack-production mm-production-statement_essential (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e25ad2b22ae15cd612c6334ae1268a9eb97a6837d9ea00446a0f0cfdff4b5fbc
      (head (compile-pack-production mm-production-statement_essential (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-0082426d817ad072153eb0355368a7898a6b58d576d02c883b3d96302d841dfd
      (head (compile-pack-production mm-production-statement_essential (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-3891c9530139538dcc3be59b0a3a2fba45314fa29154e6ae851c8fc97d7b42cb
      (head (compile-pack-production mm-production-statement_essential (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_essential) body) right) right) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-4ca92a028145617f4ff4a9dea40f52dfdc4db12c1d8cfce36cedd796c48aef86
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-def mm-production-statement_float) node) (pp-def mm-production-statement_float) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-statement_float) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const statement_float) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-cfdb437081f3ef6c81d0413d69abad2f4e063a12d01b1d4317c724ed487477b1
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-def mm-production-statement_float) body) seq) (pp-sub (pp-def mm-production-statement_float) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_float) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-8385f3c750130082c62481f3b867e105d3375ef537d6a93c3a7532f17dd6302b
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_float) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-statement_float) body) left) (pp-items-cons (pp-nonterminal (pp-def label-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-800815e1d74937ab63f881ac01c33411f7ec09cfe543ed076a7ccda7a5c90a13
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-71b55159188cc8733548ee2096d3231a7f294f427a5ae05e1dd7657528cd8aff
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-float)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-dc3ee4a8cf3138cb8db04f8f5c658dd6512c70a4c1d25b1783e276298d862e62
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) seq) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-7e4cca55931a6dd619a8eadeb899d5fb755a1eab5315b1c5982fda7f96f667eb
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7ae97df1af800c344637521a5e71c10968f38f5fd83c489ed27669e4625a68ed
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) left) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-18121fb8aaf3f4bee8c324fe6b8ab861b09f5e4206244ea855ee4072159a5a6c
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-50d080592a002680b3e0afde363f781f1c467bce02bacf5b3c8795ce4bf0f765
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-1bdeb7d98fb2bd7daec3f03271cd8dc9cc6348989d770e1a279722cf6e0caf2a
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f68a2b7b287b06b415616cfabb8217d026a522d24f743a7531c21518de2a2059
      (head (compile-pack-production mm-production-statement_float (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_float) body) right) right) right) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-c6d6dd772d921ea2b7e29661be6f13e36e8c432f444df876f5fe279b5077ea2a
      (head (compile-pack-production mm-production-statement_include (pp-production (pp-label (pp-def mm-production-statement_include) node) (pp-def mm-production-statement_include) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-statement_include) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const statement_include) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-7ecd151ab072ceb6403908cb49c81a80e70e7906df65e6239e038ce4d11fb4a3
      (head (compile-pack-production mm-production-statement_include (pp-production (pp-label (pp-sub (pp-def mm-production-statement_include) body) right) (pp-sub (pp-def mm-production-statement_include) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_include) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-1adb9440af238cbc4e69578aec3af4f7b2c1044fbb4e62eef163a226e61f228a
      (head (compile-pack-production mm-production-statement_include (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_include) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-statement_include) body) left) (pp-items-cons (pp-nonterminal (pp-def kw-include-open)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f493b8b93e7b18cdd6c5165987e23bc2a3d70c9295b18f176dfd253ac4ef5720
      (head (compile-pack-production mm-production-statement_include (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) left) (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-07b89518bbade46300eb56e030057b05dc944f0a58815b9dacc79881e998fe43
      (head (compile-pack-production mm-production-statement_include (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) left) (pp-items-cons (pp-nonterminal (pp-def include-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ee93213657520c7beeb6a4be059d9aa1f3990e7cf3e865a0c2cbfd0211e23856
      (head (compile-pack-production mm-production-statement_include (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) right) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-a99664cf8f2c61dc7c7d144f67fadb8f71ae0e5ec317aa48bb0193dd44c77c37
      (head (compile-pack-production mm-production-statement_include (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-include-close)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ffce77056030994a6fc30ae7e4dbf66071384daee94e9f2d4f68cc3554990641
      (head (compile-pack-production mm-production-statement_include (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_include) body) right) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-5961e89d26e81468e40b63d9c460a2bc3065efb1f3f6ff776ef94f1ddb87b220
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-def mm-production-statement_theorem_compressed) node) (pp-def mm-production-statement_theorem_compressed) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-statement_theorem_compressed) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const statement_theorem_compressed) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-ae60103cf572c6c42a65e278da071015f117741d3f50e5df552157d18e660c05
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-def mm-production-statement_theorem_compressed) body) seq) (pp-sub (pp-def mm-production-statement_theorem_compressed) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-dc9bf9157206fa70150c66067dac511a9bc13a38856f534271a243b4cd65bb15
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) left) (pp-items-cons (pp-nonterminal (pp-def label-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-8d49cb1b91fe88700d1e3ef3c185d27d334ec9bfbbdc5d9783d522a2d046bd71
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-57fe89ed931edeff82806b9ccf718d1cb8d0b788af8f604845be73dbe7264016
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-theorem)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-98c571f98b9f8554a1779b1eecdf6560ce933a0486b427210b9131a8f4a7cbfa
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) seq) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-5e0ad67559a049204e3326cd6804b0721947a0e467db855b1156956cbe72204b
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-f6cc6127e5fdea9869ab45779bf84d0f0e1962d0a9e194a7bb5c4cebc336a367
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-a0933fb2586176f46ebb61db784f0645b7846b399309238b0c320d75bb993efa
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-proof)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-1739aa6cf24141e51499cdd1a3150b43c91bc36a9c39971dcfb146bad2b8236a
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-f9eab62c9239d59f5b92fc4a884ce13dd96cd5e7c32084bcdaaee13ad6327987
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def left-paren)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-91296986f8713790abff4a3aada914693daf0f9e4f7dfe1f8461e88964945655
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) seq) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-217bac598f702d57ffe750e518c092bdad6b1cfad44f042bb623173aca91d56f
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-proof_header_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b025a8732b68135db8225475d474531d811e1fe8cbb06fd063f206add92a4da6
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-37d5fcb488bd5356dfd6a8b5dd9dc96378ef51d60225f21d465e1d8ee1fff3f6
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def right-paren)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2086928cebcaaa1d7afefd21190186ab6b238c7235e416e2b943f4947b6eda11
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) left) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-193b6b12c80b83dc56f0cdb32eb5bef377d003fae19c911db8029fd7c08a6272
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-compressed_word_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a47a30b06ae110c30b76eba08f035f027d23baa4a696edd71ff02ab6d00593be
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-366d31c1e4e0c63b5c8d09fb4f84456387fd3a856af01cdb9b091ff1ee575892
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-e1a4fa334e0de924b322c34933a041c99d75880962cf1f0663bd126b1650aca5
      (head (compile-pack-production mm-production-statement_theorem_compressed (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_compressed) body) right) right) right) right) right) right) right) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-d5b157eed774ecd021e3a4296d8ebf3091ebf2e91cc345a7ae6d73ecfbb6528f
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-def mm-production-statement_theorem_normal) node) (pp-def mm-production-statement_theorem_normal) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-statement_theorem_normal) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const statement_theorem_normal) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-52807895de97364a81c1ec52a46f874cd558ba7ca0049062ca12bf3837571a9c
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-def mm-production-statement_theorem_normal) body) seq) (pp-sub (pp-def mm-production-statement_theorem_normal) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-d2d17aff61006c54f438ca9d24e8d9b01cdc6628adb1624fdff4563287e09af5
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) left) (pp-items-cons (pp-nonterminal (pp-def label-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d9b8c4ad77888e4c79235322bcb7555a4793279ca3cea07416b67b34dfee2bbf
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-a6ecd75eec48140a2a3e092ebd7e969e991537fd944e6e528cc1ef933e3c780e
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-theorem)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-528ca42c4fa41fc247c6e243d573639a34ad0008c3a0af79586d352b2ea76920
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) seq) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-6899d72469e7f6f6a7e8bb7b5c348b5a852aa20d18cbc3c2c04923559d4199dd
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-8bdabe292273c9e7df7b2cd478d747e0f1abc4a649f22fd859681591366a371a
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-df062333ca89763962b5727e4599331f93a8a160b4839fa2469f25e7aa3d5094
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-proof)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c3671bcbaf0a40846410179acc9f5116d2ccc8162fe8f3d972867b7165f78255
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) left) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b1003eb5e3c2e709da173ff89dc8da0200cb46c36b0c4455149717b7719c709c
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-proof_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-1231fae1dc2c2e4a5861fbfc7141e816ef89569022cf3cd194788144d284bd6e
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) right) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-110c27285d2bb94ed443c65c601b4ae208d4982e561afea08ab3a0fc31a1babc
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c02cac4bd0304f2b9bc6fed8e3ed62c061e26c153c64d248758252ada11fd4c5
      (head (compile-pack-production mm-production-statement_theorem_normal (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_theorem_normal) body) right) right) right) right) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-48e61c96893db2001b8f438d7515f3309c4b67e429367f691c577ef3d9821e4c
      (head (compile-pack-production mm-production-statement_var (pp-production (pp-label (pp-def mm-production-statement_var) node) (pp-def mm-production-statement_var) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-statement_var) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const statement_var) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-5bb6ba1d033d5f58d5b8af3fe999fb844fb9107967d917b9560dabcf302e842a
      (head (compile-pack-production mm-production-statement_var (pp-production (pp-label (pp-sub (pp-def mm-production-statement_var) body) right) (pp-sub (pp-def mm-production-statement_var) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_var) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-3608c7c1a38899640fa2f8e2ccd7e7863301dbef9dc05fe7ec7a17fc18cb5ac2
      (head (compile-pack-production mm-production-statement_var (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_var) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-statement_var) body) left) (pp-items-cons (pp-nonterminal (pp-def kw-variable)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d49e368be28840026645d130dd431493edcd91fd8529be788c7bee6e105628fb
      (head (compile-pack-production mm-production-statement_var (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) left) (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2d8d4cffc5e484101096ff07808c67f545adb048b6e91addea523ca73e02b5d9
      (head (compile-pack-production mm-production-statement_var (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-c99f9980225dd63cc9be8ae2eb42b9c7cb009e2348eabb6c810d2f2475820fcc
      (head (compile-pack-production mm-production-statement_var (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) right) (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) right)) pp-items-nil)) (pa-slot (q-succ q-zero)))))
      (body))
    (rule parser-pack-fact-d3bc6714e94ab023d3c17ada77cd8f88a6bb05909289dd153c7cda860c96cb73
      (head (compile-pack-production mm-production-statement_var (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) left) (pp-items-cons (pp-nonterminal (pp-def kw-end)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-aa14463890d2e2c03ddd31855d457b2cd028c4887a41a09626a824d137c4bb38
      (head (compile-pack-production mm-production-statement_var (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-sub (pp-def mm-production-statement_var) body) right) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-7664df969a2b818a4cede29e6fe38cc2e0a722c0f2466a3f608cd795899064b7
      (head (compile-pack-production mm-production-symbols_more (pp-production (pp-label (pp-def mm-production-symbols_more) node) (pp-def mm-production-symbols_more) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-symbols_more) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const symbols_more) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-acac78e8f3de4c39640bd5b8be8e39c885bae36e98b6d5e32b8d00d5311a1c83
      (head (compile-pack-production mm-production-symbols_more (pp-production (pp-label (pp-sub (pp-def mm-production-symbols_more) body) seq) (pp-sub (pp-def mm-production-symbols_more) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right)) pp-items-nil)) (pa-apply pair (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-5259c73b50100ad3aa688aea86243224be53c23c14f203e6a0910b84dafdb664
      (head (compile-pack-production mm-production-symbols_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) left) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-51d76832d0f384b0a2e47ae8e0d048d49efa81432ef0d88c3032ba6314648086
      (head (compile-pack-production mm-production-symbols_more (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) left) (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-2de731a6c64cde6e724747d8eba3b6fbe5750958dbf30781185a75659972c27e
      (head (compile-pack-production mm-production-symbols_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) left) ref) (pp-sub (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) left) (pp-items-cons (pp-nonterminal (pp-def mm-category-symbol_list)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ddc62a5ddca82ffadc0900b20b767f2375f1189659078a6edca2caf7db6e03a3
      (head (compile-pack-production mm-production-symbols_more (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) right) eps) (pp-sub (pp-sub (pp-sub (pp-def mm-production-symbols_more) body) right) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-43488550a0786a6e44240cd9d83f9022fcec7f6a3dc260f5a487812b2c1ac827
      (head (compile-pack-production mm-production-symbols_one (pp-production (pp-label (pp-def mm-production-symbols_one) node) (pp-def mm-production-symbols_one) (pp-items-cons (pp-nonterminal (pp-sub (pp-def mm-production-symbols_one) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const symbols_one) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-9006f00218b27b9ecaf17cba37a26dbb828941fabbd745ca606138fb672bb031
      (head (compile-pack-production mm-production-symbols_one (pp-production (pp-label (pp-sub (pp-def mm-production-symbols_one) body) left) (pp-sub (pp-def mm-production-symbols_one) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-symbols_one) body) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def mm-production-symbols_one) body) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-868e32f91a69e3f81b900aceb542f25f0a40dead01621416dfab9bbb68aa6110
      (head (compile-pack-production mm-production-symbols_one (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-symbols_one) body) left) ref) (pp-sub (pp-sub (pp-def mm-production-symbols_one) body) left) (pp-items-cons (pp-nonterminal (pp-def symbol-token)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-cbe2f22a2f109f98e5d0588f77fa0088d1b6a79d7fec64131e52c75fa896eadd
      (head (compile-pack-production mm-production-symbols_one (pp-production (pp-label (pp-sub (pp-sub (pp-def mm-production-symbols_one) body) right) eps) (pp-sub (pp-sub (pp-def mm-production-symbols_one) body) right) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-b8dc2e3c81921d6952fa5a940da019bcf25cd29763e6cf9f77cdabb2b70e049a
      (head (compile-pack-production proof-label-token (pp-production (pp-label (pp-def proof-label-token) left) (pp-def proof-label-token) (pp-items-cons (pp-nonterminal (pp-sub (pp-def proof-label-token) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def proof-label-token) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-3916bfe0e4908948e6783018a6f8d946ea40a8a4b7ddf1482f40d572834ff84e
      (head (compile-pack-production proof-label-token (pp-production (pp-label (pp-sub (pp-def proof-label-token) left) ref) (pp-sub (pp-def proof-label-token) left) (pp-items-cons (pp-nonterminal (pp-def proof-label-token-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-5698a30b20b904ecf54e89507bf826e0dc76592ee33ad90816fab7fb416d0d88
      (head (compile-pack-production proof-label-token (pp-production (pp-label (pp-sub (pp-def proof-label-token) right) ref) (pp-sub (pp-def proof-label-token) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-410975307fbde8645bbec8d8025e0a90386e7e4f20aded707dcda1b3cbf14d00
      (head (compile-pack-production proof-label-token-lexeme (pp-production (pp-label (pp-def proof-label-token-lexeme) left) (pp-def proof-label-token-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def proof-label-token-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def proof-label-token-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-6f981ece9e5f647f432f63313e2f1dd14601f012b056095fbc8554c4c3d6969a
      (head (compile-pack-production proof-label-token-lexeme (pp-production (pp-label (pp-sub (pp-def proof-label-token-lexeme) left) node) (pp-sub (pp-def proof-label-token-lexeme) left) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const proof-label-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-d427ddd9a41f40b817ed77a95fe69c59fdc73d314e94b6b0e1596d0e0d4d208a
      (head (compile-pack-production proof-label-token-lexeme (pp-production (pp-label (pp-sub (pp-def proof-label-token-lexeme) right) ref) (pp-sub (pp-def proof-label-token-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7c43e75186ac20a8a8b126500d5e08aa1903fdde412bb0ac98389c15cb62ed57
      (head (compile-pack-production proof-label-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) plus-more) (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) body)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-e45845022f20a56e6f8beff7ed2233b4a83c92ad710980b62fdf551e41292dd6
      (head (compile-pack-production proof-label-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) plus-one) (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) body)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-788dc233a74bcf215d1a34ad0489ad05f027e7507ce4f1174db22ca4293c91d2
      (head (compile-pack-production proof-label-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) body) class) (pp-sub (pp-sub (pp-sub (pp-def proof-label-token-lexeme) left) body) body) (pp-items-cons (pp-terminal (pp-terminal-class label-char)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-05dd2cd5ab7310af84068b1b5c088842d5959b04a283cef5f177fe19f8ac6c3e
      (head (compile-pack-production right-paren (pp-production (pp-label (pp-def right-paren) left) (pp-def right-paren) (pp-items-cons (pp-nonterminal (pp-sub (pp-def right-paren) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def right-paren) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-13e75d49f3289ddf76281d67f46876654bd3e384eccb880e4ce4efd39988dd56
      (head (compile-pack-production right-paren (pp-production (pp-label (pp-sub (pp-def right-paren) left) ref) (pp-sub (pp-def right-paren) left) (pp-items-cons (pp-nonterminal (pp-def right-paren-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-5305bfec069721c00a49e9b591019924c26a2b09cba21caed1f9a6c9a3bb2743
      (head (compile-pack-production right-paren (pp-production (pp-label (pp-sub (pp-def right-paren) right) ref) (pp-sub (pp-def right-paren) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-7d3cc5a9577402ab02459ed3c5c3553d5e10fa42dc277584798a5808339b65d0
      (head (compile-pack-production right-paren-lexeme (pp-production (pp-label (pp-def right-paren-lexeme) left) (pp-def right-paren-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def right-paren-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def right-paren-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-ae81e24a118595d9fd458ccda1e8ccce6c1defd0964e5e10f8c5eda7920d6708
      (head (compile-pack-production right-paren-lexeme (pp-production (pp-label (pp-sub (pp-def right-paren-lexeme) left) lit) (pp-sub (pp-def right-paren-lexeme) left) (pp-items-cons (pp-terminal (pp-terminal-char (cp 41))) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-cac3374ba25707a8aed06a883f6fcd62a6f4843ac808e249c5b6ac439ebe5605
      (head (compile-pack-production right-paren-lexeme (pp-production (pp-label (pp-sub (pp-def right-paren-lexeme) right) ref) (pp-sub (pp-def right-paren-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-47a7d094b6f65a74ad099165165f0ae9ebdafd4648c30c52cd109688055f0430
      (head (compile-pack-production skip (pp-production (pp-label (pp-def skip) star-cons) (pp-def skip) (pp-items-cons (pp-nonterminal (pp-sub (pp-def skip) body)) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-66922de98e97ea187326368c19a8ac6e45166aa6cad4a3f7f5137758f1d6fb1d
      (head (compile-pack-production skip (pp-production (pp-label (pp-def skip) star-empty) (pp-def skip) pp-items-nil (pa-const nil))))
      (body))
    (rule parser-pack-fact-5e13de98e7fcd61eba83a938ffac0ec05ab1a86bd6558517d448d2e84f56d086
      (head (compile-pack-production skip (pp-production (pp-label (pp-sub (pp-def skip) body) ref) (pp-sub (pp-def skip) body) (pp-items-cons (pp-nonterminal (pp-def skip-unit)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-4d0863e9fac3f2a4442724595484d64026cf4da9c112f697e20846a032257daf
      (head (compile-pack-production skip-unit (pp-production (pp-label (pp-def skip-unit) alt-left) (pp-def skip-unit) (pp-items-cons (pp-nonterminal (pp-sub (pp-def skip-unit) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-41a7376ac9da1472edb0e917450b6298d6818b5565ac0eb96d8c0044a649c8ae
      (head (compile-pack-production skip-unit (pp-production (pp-label (pp-def skip-unit) alt-right) (pp-def skip-unit) (pp-items-cons (pp-nonterminal (pp-sub (pp-def skip-unit) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-168e5322443f37389096240b1caf317dc6994fcbf3b328097097f414284375a6
      (head (compile-pack-production skip-unit (pp-production (pp-label (pp-sub (pp-def skip-unit) left) class) (pp-sub (pp-def skip-unit) left) (pp-items-cons (pp-terminal (pp-terminal-class whitespace)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-409b2f88ef842f580e1d32e83610051c8d941e35f8c8c6cde4060ed5de5ca020
      (head (compile-pack-production skip-unit (pp-production (pp-label (pp-sub (pp-def skip-unit) right) ref) (pp-sub (pp-def skip-unit) right) (pp-items-cons (pp-nonterminal (pp-def comment)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-86d65815a4b3113191dccd88af48712c96e290f3cb196da7a0c16b3967b968cd
      (head (compile-pack-production symbol-token (pp-production (pp-label (pp-def symbol-token) left) (pp-def symbol-token) (pp-items-cons (pp-nonterminal (pp-sub (pp-def symbol-token) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def symbol-token) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-08a422c122921b83e0124e9e031e68e66156b1f99c2c3c08e66df9fe27ed4e20
      (head (compile-pack-production symbol-token (pp-production (pp-label (pp-sub (pp-def symbol-token) left) ref) (pp-sub (pp-def symbol-token) left) (pp-items-cons (pp-nonterminal (pp-def symbol-token-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-eb71dcfe2f7257d754fbc10193e20588ce710f72aaa3e78e6fad58c75ce8e332
      (head (compile-pack-production symbol-token (pp-production (pp-label (pp-sub (pp-def symbol-token) right) ref) (pp-sub (pp-def symbol-token) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-578c748f377a8de3ab4781be815b67cd7d0d05a4f3eb0438df87fc812f22ee23
      (head (compile-pack-production symbol-token-lexeme (pp-production (pp-label (pp-def symbol-token-lexeme) left) (pp-def symbol-token-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def symbol-token-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def symbol-token-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-297d731e70476da913162db60f1db637e2970813fb9b6009d375e3877960037a
      (head (compile-pack-production symbol-token-lexeme (pp-production (pp-label (pp-sub (pp-def symbol-token-lexeme) left) node) (pp-sub (pp-def symbol-token-lexeme) left) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const symbol-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-7a563f85a5316c29435ec8fa36665a50107a2f873f3575c1ee19cbe827bdec81
      (head (compile-pack-production symbol-token-lexeme (pp-production (pp-label (pp-sub (pp-def symbol-token-lexeme) right) ref) (pp-sub (pp-def symbol-token-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d8f9a745a699f38a56d5cd5391af50d6d1d8eeede74d292d06bd97205c43c0b4
      (head (compile-pack-production symbol-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) plus-more) (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) body)) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-b46cf0a564d100c2af8ebccd82dfae31902a3a6ffc70875193a9034e0a897876
      (head (compile-pack-production symbol-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) plus-one) (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) body)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-a7902bacc1ead04df59a89fdcc8c2c56288b789279c6fd232bac078f65d8bdf2
      (head (compile-pack-production symbol-token-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) body) class) (pp-sub (pp-sub (pp-sub (pp-def symbol-token-lexeme) left) body) body) (pp-items-cons (pp-terminal (pp-terminal-class math-symbol-char)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-856f0742da3991c713faa4d81708e9736de1ba0fd9f869c5e989369f6a3370a6
      (head (compile-pack-production token-separator (pp-production (pp-label (pp-def token-separator) alt-left) (pp-def token-separator) (pp-items-cons (pp-nonterminal (pp-sub (pp-def token-separator) left)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-a839bfc45f40158b5c294f7e4a8d69776189310686da772dcb07894e314ec116
      (head (compile-pack-production token-separator (pp-production (pp-label (pp-def token-separator) alt-right) (pp-def token-separator) (pp-items-cons (pp-nonterminal (pp-sub (pp-def token-separator) right)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-aead46a32018ac7d97cfae2541cddd9a3439efaa5633098a7c13caff86b43eeb
      (head (compile-pack-production token-separator (pp-production (pp-label (pp-sub (pp-def token-separator) left) class) (pp-sub (pp-def token-separator) left) (pp-items-cons (pp-terminal (pp-terminal-class whitespace)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-27bfdeddaab38ea51087fcd56c213443bc239592e30731bf9743acb9d85e8f82
      (head (compile-pack-production token-separator (pp-production (pp-label (pp-sub (pp-def token-separator) right) eof) (pp-sub (pp-def token-separator) right) (pp-items-cons (pp-terminal pp-terminal-eof) pp-items-nil) (pa-const eof))))
      (body))
    (rule parser-pack-fact-20b4d5e308f29641f0c19d3e0d6aeee46769f742f246aa11032179b8a6fad770
      (head (compile-pack-production unknown-proof (pp-production (pp-label (pp-def unknown-proof) left) (pp-def unknown-proof) (pp-items-cons (pp-nonterminal (pp-sub (pp-def unknown-proof) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def unknown-proof) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-354e867ac828f6a579247b10bb2c8398217b0c1294dcc3c30c58b7aa151264e2
      (head (compile-pack-production unknown-proof (pp-production (pp-label (pp-sub (pp-def unknown-proof) left) ref) (pp-sub (pp-def unknown-proof) left) (pp-items-cons (pp-nonterminal (pp-def unknown-proof-lexeme)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-b95acc3b4f31e84dd9e6eef5b1bac7887d0d37ede051c53c7bcc94e1b0b27eaf
      (head (compile-pack-production unknown-proof (pp-production (pp-label (pp-sub (pp-def unknown-proof) right) ref) (pp-sub (pp-def unknown-proof) right) (pp-items-cons (pp-nonterminal (pp-def skip)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-be2fcaf2879db5df267da7307a738de55dff1044bc59b9e8f2d962c7d4054511
      (head (compile-pack-production unknown-proof-lexeme (pp-production (pp-label (pp-def unknown-proof-lexeme) left) (pp-def unknown-proof-lexeme) (pp-items-cons (pp-nonterminal (pp-sub (pp-def unknown-proof-lexeme) left)) (pp-items-cons (pp-nonterminal (pp-sub (pp-def unknown-proof-lexeme) right)) pp-items-nil)) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-247138329a2fa7e3267fc98713e609dc90fd03f550e82c02da155be0193527ab
      (head (compile-pack-production unknown-proof-lexeme (pp-production (pp-label (pp-sub (pp-def unknown-proof-lexeme) left) node) (pp-sub (pp-def unknown-proof-lexeme) left) (pp-items-cons (pp-nonterminal (pp-sub (pp-sub (pp-def unknown-proof-lexeme) left) body)) pp-items-nil) (pa-apply node (pa-cons (pa-const unknown-proof-token) (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))
    (rule parser-pack-fact-2fa5b6dff37588849e58e8e7aa0e9302fa0eb3a0319c337beaa734253e073c92
      (head (compile-pack-production unknown-proof-lexeme (pp-production (pp-label (pp-sub (pp-def unknown-proof-lexeme) right) ref) (pp-sub (pp-def unknown-proof-lexeme) right) (pp-items-cons (pp-nonterminal (pp-def token-separator)) pp-items-nil) (pa-slot q-zero))))
      (body))
    (rule parser-pack-fact-d39cd4bc99abdeb841585d860fb520c2bcb235216447dff6d6b41fc643aa5833
      (head (compile-pack-production unknown-proof-lexeme (pp-production (pp-label (pp-sub (pp-sub (pp-def unknown-proof-lexeme) left) body) lit) (pp-sub (pp-sub (pp-def unknown-proof-lexeme) left) body) (pp-items-cons (pp-terminal (pp-terminal-char (cp 63))) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-66082966415f497b402626645f742abe719518200457085e665e49ddb3ccc93a
      (head (compile-pack-production whitespace1 (pp-production (pp-label (pp-def whitespace1) plus-more) (pp-def whitespace1) (pp-items-cons (pp-nonterminal (pp-sub (pp-def whitespace1) body)) (pp-items-cons (pp-nonterminal (pp-def whitespace1)) pp-items-nil)) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))))
      (body))
    (rule parser-pack-fact-149d0de356dcefcf805f763d7dab0eeb9cc50f3d80bebd83c04099d849d36e11
      (head (compile-pack-production whitespace1 (pp-production (pp-label (pp-def whitespace1) plus-one) (pp-def whitespace1) (pp-items-cons (pp-nonterminal (pp-sub (pp-def whitespace1) body)) pp-items-nil) (pa-apply cons (pa-cons (pa-slot q-zero) (pa-cons (pa-const nil) pa-nil))))))
      (body))
    (rule parser-pack-fact-151c0fe993eef04470440ac748da92e27ce60f99d7ccfd397dbf78ba7a29ee17
      (head (compile-pack-production whitespace1 (pp-production (pp-label (pp-sub (pp-def whitespace1) body) class) (pp-sub (pp-def whitespace1) body) (pp-items-cons (pp-terminal (pp-terminal-class whitespace)) pp-items-nil) (pa-slot q-zero))))
      (body))
  ))
