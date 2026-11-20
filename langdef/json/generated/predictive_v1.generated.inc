#define JD_IMPLEMENTATION_ARTIFACT_0 UINT32_C(0xd5637494)
#define JD_IMPLEMENTATION_ARTIFACT_1 UINT32_C(0x90db9817)
#define JD_IMPLEMENTATION_ARTIFACT_2 UINT32_C(0x6c04f760)
#define JD_IMPLEMENTATION_ARTIFACT_3 UINT32_C(0x06e064c5)
#define JD_IMPLEMENTATION_ARTIFACT_4 UINT32_C(0x000eaaf0)
#define JD_IMPLEMENTATION_ARTIFACT_5 UINT32_C(0xdde37e3f)
#define JD_IMPLEMENTATION_ARTIFACT_6 UINT32_C(0x1a00880a)
#define JD_IMPLEMENTATION_ARTIFACT_7 UINT32_C(0xa52dd55d)
_Static_assert(JD_LEXICAL_ARTIFACT_0 == JD_PREDICTIVE_ARTIFACT_0 && JD_PREDICTIVE_ARTIFACT_0 == JD_IMPLEMENTATION_ARTIFACT_0, "JSON generated artifacts differ");
_Static_assert(JD_LEXICAL_ARTIFACT_1 == JD_PREDICTIVE_ARTIFACT_1 && JD_PREDICTIVE_ARTIFACT_1 == JD_IMPLEMENTATION_ARTIFACT_1, "JSON generated artifacts differ");
_Static_assert(JD_LEXICAL_ARTIFACT_2 == JD_PREDICTIVE_ARTIFACT_2 && JD_PREDICTIVE_ARTIFACT_2 == JD_IMPLEMENTATION_ARTIFACT_2, "JSON generated artifacts differ");
_Static_assert(JD_LEXICAL_ARTIFACT_3 == JD_PREDICTIVE_ARTIFACT_3 && JD_PREDICTIVE_ARTIFACT_3 == JD_IMPLEMENTATION_ARTIFACT_3, "JSON generated artifacts differ");
_Static_assert(JD_LEXICAL_ARTIFACT_4 == JD_PREDICTIVE_ARTIFACT_4 && JD_PREDICTIVE_ARTIFACT_4 == JD_IMPLEMENTATION_ARTIFACT_4, "JSON generated artifacts differ");
_Static_assert(JD_LEXICAL_ARTIFACT_5 == JD_PREDICTIVE_ARTIFACT_5 && JD_PREDICTIVE_ARTIFACT_5 == JD_IMPLEMENTATION_ARTIFACT_5, "JSON generated artifacts differ");
_Static_assert(JD_LEXICAL_ARTIFACT_6 == JD_PREDICTIVE_ARTIFACT_6 && JD_PREDICTIVE_ARTIFACT_6 == JD_IMPLEMENTATION_ARTIFACT_6, "JSON generated artifacts differ");
_Static_assert(JD_LEXICAL_ARTIFACT_7 == JD_PREDICTIVE_ARTIFACT_7 && JD_PREDICTIVE_ARTIFACT_7 == JD_IMPLEMENTATION_ARTIFACT_7, "JSON generated artifacts differ");
/* Generated predictive decisions from LanguageDef + elaboration. */
#define JD_OBJECT_OPEN 123u
#define JD_OBJECT_CLOSE 125u
#define JD_ARRAY_OPEN 91u
#define JD_ARRAY_CLOSE 93u
#define JD_MEMBER_COLON 58u
#define JD_MEMBER_COMMA 44u
#define JD_ELEMENT_COMMA 44u
static JsonLookaheadV1 jd_value_lookahead(const JsonDirectV1 *p) {
if (p->at == p->len) goto invalid;
switch(p->bytes[p->at]) {
case 102:
if (5u > p->len-p->at || memcmp(p->bytes+p->at,"false",5)) break;
return JD_LOOK_FALSE;
case 110:
if (4u > p->len-p->at || memcmp(p->bytes+p->at,"null",4)) break;
return JD_LOOK_NULL;
case 116:
if (4u > p->len-p->at || memcmp(p->bytes+p->at,"true",4)) break;
return JD_LOOK_TRUE;
case 123:
return JD_LOOK_OBJECT;
case 91:
return JD_LOOK_ARRAY;
case 45:
case 48:
case 49:
case 50:
case 51:
case 52:
case 53:
case 54:
case 55:
case 56:
case 57:
return JD_LOOK_NUMBER;
case 34:
return JD_LOOK_STRING;
default: break; }
invalid: return JD_LOOK_ERROR;
}
static JsonStartV1 jd_value_start(JsonDirectV1 *p, Atom **value) {
switch (jd_value_lookahead(p)) {
case JD_LOOK_FALSE:
p->at += 5; p->scalar += 5;
*value = p->ordinary ? (p->petta ? atom_symbol(p->arena,"false") : atom_bool(p->arena,false)) : jd_one(p,JD_BOOL,p->petta ? atom_symbol(p->arena,"false") : atom_bool(p->arena,false)); return JD_START_SCALAR;
case JD_LOOK_NULL:
p->at += 4; p->scalar += 4;
*value = jd_head(p,JD_NULL); return JD_START_SCALAR;
case JD_LOOK_TRUE:
p->at += 4; p->scalar += 4;
*value = p->ordinary ? (p->petta ? atom_symbol(p->arena,"true") : atom_bool(p->arena,true)) : jd_one(p,JD_BOOL,p->petta ? atom_symbol(p->arena,"true") : atom_bool(p->arena,true)); return JD_START_SCALAR;
case JD_LOOK_OBJECT:
return JD_START_OBJECT;
case JD_LOOK_ARRAY:
return JD_START_ARRAY;
case JD_LOOK_NUMBER:
*value = jd_number(p); return *value ? JD_START_SCALAR : JD_START_ERROR;
case JD_LOOK_STRING:
*value = jd_string(p); return *value ? JD_START_SCALAR : JD_START_ERROR;
case JD_LOOK_ERROR: break; }
jd_error(p,CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,"a JSON value");
return JD_START_ERROR;
}
