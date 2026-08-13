#include "generated/he_typing_closed_ground_core_source_binding_v1.generated.h"
#include "he_typing_authority.h"

const CettaNikDirectSourceBindingV1
    he_typing_closed_ground_core_source_binding_v1 = {
        .authority = &cetta_he_typing_core_direct_authority_v1,
        .schema_id = "finite-horn-gslt-v1",
        .presentation_id = "he-typing-closed-ground-core-v1",
        .semantic_scope = "he.typing.closed-ground-decision-core",
        .source_sha256 = "0cb6a64037f244588a3278aa679db9d88fb8fa46c79663ba38e41cf224413531",
        .package_sha256 = "8faab873ad2d58779fcc9331e3813455dcbf6e5811c9a078b6a2f9f44eac425d",
        .coverage = CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT,
        .mode = "direct-decision",
        .certificate_policy = "none",
        .fiber = "he",
        .default_outcome = "HCheckUndetermined",
        .native_projection = "pending",
        .presentation_status = "AUTHORED_FRAGMENT",
    };
