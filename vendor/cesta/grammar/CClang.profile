profile CClang;
include "CGNU.profile";
-- Clang's GNU dialect lexes the ISO/IEC TS 18661-3 interchange type names as
-- identifiers on this target, and the C library declares them as typedef names.
lexical identifier "_Float32";
lexical identifier "_Float64";
lexical identifier "_Float128";
lexical identifier "_Float32x";
lexical identifier "_Float64x";
lexical identifier "_Float128x";
