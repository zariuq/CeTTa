#include "generated/prime_nik_authorities_v1.generated.h"

const CettaNikAuthorityV1 cetta_prime_nik_authorities_v1[] = {
    {
        .alias = "DTT",
        .system_id = "prime.dtt.calibration",
        .revision = "1",
        .digest = "fd25250f162952bb4bd227fb3406755e894cee1a5e53e486299dcf4fd8a4c2fe",
        .presentation_metta = "(GPresentation (LCons (CDecl \"C\" 0) (LCons (CDecl \"Z\" 0) (LCons (CDecl \"N\" 0) (LCons (CDecl \"i\" 0) (LCons (CDecl \"F\" 2) (LCons (CDecl \"@\" 2) LNil)))))) (LCons (JDecl \"T\" 3) LNil) (LCons (GRule \"z\" LNil LNil (PApp \"T\" (LCons (PApp \"C\" LNil) (LCons (PApp \"Z\" LNil) (LCons (PApp \"N\" LNil) LNil))))) (LCons (GRule \"j\" LNil LNil (PApp \"T\" (LCons (PApp \"C\" LNil) (LCons (PApp \"i\" LNil) (LCons (PApp \"F\" (LCons (PApp \"N\" LNil) (LCons (PApp \"N\" LNil) LNil))) LNil))))) (LCons (GRule \"k\" (LCons (Formal \"f\" 0) (LCons (Formal \"x\" 0) (LCons (Formal \"d\" 0) (LCons (Formal \"c\" 0) LNil)))) (LCons (PApp \"T\" (LCons (PApp \"C\" LNil) (LCons (FVar \"f\") (LCons (PApp \"F\" (LCons (FVar \"d\") (LCons (FVar \"c\") LNil))) LNil)))) (LCons (PApp \"T\" (LCons (PApp \"C\" LNil) (LCons (FVar \"x\") (LCons (FVar \"d\") LNil)))) LNil)) (PApp \"T\" (LCons (PApp \"C\" LNil) (LCons (PApp \"@\" (LCons (FVar \"f\") (LCons (FVar \"x\") LNil))) (LCons (FVar \"c\") LNil))))) LNil))))",
        .positive_goal_metta = "(PApp \"T\" (LCons (PApp \"C\" LNil) (LCons (PApp \"@\" (LCons (PApp \"i\" LNil) (LCons (PApp \"Z\" LNil) LNil))) (LCons (PApp \"N\" LNil) LNil))))",
        .positive_proof_metta = "(GProof (GRuleInst \"k\" (LCons (PApp \"i\" LNil) (LCons (PApp \"Z\" LNil) (LCons (PApp \"N\" LNil) (LCons (PApp \"N\" LNil) LNil))))) (PrCons (GProof (GRuleInst \"j\" LNil) PrNil) (PrCons (GProof (GRuleInst \"z\" LNil) PrNil) PrNil)))",
    },
    {
        .alias = "HOTG",
        .system_id = "prime.hotg.calibration",
        .revision = "1",
        .digest = "c37fdb3dd1a4b820e7adbdf06c1ced2753ad59ec034b4504a86a71978687984c",
        .presentation_metta = "(GPresentation (LCons (CDecl \"I\" 2) (LCons (CDecl \"U\" 1) (LCons (CDecl \"C\" 1) (LCons (CDecl \"W\" 1) LNil)))) (LCons (JDecl \"H\" 1) LNil) (LCons (GRule \"q\" (LCons (Formal \"n\" 0) LNil) LNil (PApp \"H\" (LCons (PApp \"I\" (LCons (FVar \"n\") (LCons (PApp \"U\" (LCons (FVar \"n\") LNil)) LNil))) LNil))) (LCons (GRule \"r\" (LCons (Formal \"n\" 0) LNil) LNil (PApp \"H\" (LCons (PApp \"C\" (LCons (PApp \"U\" (LCons (FVar \"n\") LNil)) LNil)) LNil))) (LCons (GRule \"s\" (LCons (Formal \"u\" 0) (LCons (Formal \"x\" 0) LNil)) (LCons (PApp \"H\" (LCons (PApp \"C\" (LCons (FVar \"u\") LNil)) LNil)) (LCons (PApp \"H\" (LCons (PApp \"I\" (LCons (FVar \"x\") (LCons (FVar \"u\") LNil))) LNil)) LNil)) (PApp \"H\" (LCons (PApp \"I\" (LCons (PApp \"W\" (LCons (FVar \"x\") LNil)) (LCons (FVar \"u\") LNil))) LNil))) LNil))))",
        .positive_goal_metta = "(PApp \"H\" (LCons (PApp \"I\" (LCons (PApp \"W\" (LCons (PApp \"n\" LNil) LNil)) (LCons (PApp \"U\" (LCons (PApp \"n\" LNil) LNil)) LNil))) LNil))",
        .positive_proof_metta = "(GProof (GRuleInst \"s\" (LCons (PApp \"U\" (LCons (PApp \"n\" LNil) LNil)) (LCons (PApp \"n\" LNil) LNil))) (PrCons (GProof (GRuleInst \"r\" (LCons (PApp \"n\" LNil) LNil)) PrNil) (PrCons (GProof (GRuleInst \"q\" (LCons (PApp \"n\" LNil) LNil)) PrNil) PrNil)))",
    },
};

const size_t cetta_prime_nik_authorities_v1_count =
    sizeof(cetta_prime_nik_authorities_v1) / sizeof(cetta_prime_nik_authorities_v1[0]);

const char cetta_prime_nik_authorities_v1_catalog_sha256[] = "0508745b3c3fcad94d834240426199ea7b716e2d13c43f886865179f9260e336";
