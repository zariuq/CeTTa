profile CClangC11;
include "CGNUC11.profile";
-- Clang's C11 mode keeps its GNU extensions and the same interchange-type lexing.
lexical identifier "_Float32";
lexical identifier "_Float64";
lexical identifier "_Float128";
lexical identifier "_Float32x";
lexical identifier "_Float64x";
lexical identifier "_Float128x";
