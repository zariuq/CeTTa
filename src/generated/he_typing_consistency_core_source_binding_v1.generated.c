#include "generated/he_typing_consistency_core_source_binding_v1.generated.h"
#include "he_typing_authority.h"

const CettaNikDirectSourceBindingV1
    he_typing_consistency_core_source_binding_v1 = {
        .authority = &cetta_he_typing_core_direct_authority_v1,
        .schema_id = "finite-horn-gslt-v1",
        .presentation_id = "he-typing-consistency-core",
        .semantic_scope = "he.typing.consistency-core",
        .source_sha256 = "146293a1c987c8f63bbd6dad49f4bdd5626df86480a3329df9c5f5b1832e5af8",
        .package_sha256 = "5c6829294e2b899dd1ef1068ea4d68a656a627c4911d075b76e41fb291c89735",
        .coverage = CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT,
        .mode = "direct-decision",
        .certificate_policy = "none",
        .fiber = "he",
        .default_outcome = "HCheckUndetermined",
        .native_projection = "pending",
        .presentation_status = "AUTHORED_FRAGMENT",
    };
