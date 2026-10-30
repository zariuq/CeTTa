profile CGNUC11;
include "CGNU.profile";
-- GCC and Clang retain underscored extensions in -std=c11 mode.
-- These unadorned spellings remain ordinary ISO identifiers in that mode.
lexical identifier "asm";
lexical identifier "typeof";
