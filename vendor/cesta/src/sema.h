/* Internal interface of the C semantic layer: name binding (bind.c), the C
   type language and target ABI (ctype.c), and elaboration (elaborate.c).
   The AST is unchanged; every semantic fact is an annotation keyed by node. */
#ifndef CESTA_SEMA_H
#define CESTA_SEMA_H

#include "cesta.h"

/* Constructor labels the semantic layer interprets. */
#define CE_SEMA_LABELS(X) \
    X(TranslationUnit) X(GNUEmptyTranslationUnit) X(ExternalFunction) X(ExternalDeclaration) \
    X(GNUExternalPragma) X(GNUEmptyExternalDeclaration) X(GNUFileAsm) X(FunctionDefinition) \
    X(GNUFunctionExtension) X(Declaration) X(StaticAssertionDeclaration) X(GNUDeclarationExtension) \
    X(StaticAssertion) X(UniqueDeclarationSpecifiers) X(PrimitiveDeclarationSpecifiers) \
    X(PrefixedDeclarationSpecifiers) X(GNUAttributeSpecifiers) X(PrimitiveSpecifierTail) X(NonTypeSpecifierTail) \
    X(NoInitDeclarators) X(InitDeclarators) X(InitDeclarator) X(InitDeclaratorValue) \
    X(StorageSpecifier) X(QualifierSpecifier) X(FunctionSpecifier) X(AlignmentSpecifier) \
    X(Typedef) X(Extern) X(Static) X(ThreadLocal) X(Auto) X(Register) X(GNUThread) \
    X(Void) X(Bool) X(AtomicType) X(StructType) X(EnumType) X(TypedefType) X(Char) X(Short) \
    X(Int) X(Long) X(Float) X(Double) X(Signed) X(Unsigned) X(Complex) X(GNUInt128) X(GNUSigned) \
    X(GNUAutoType) X(GNUFloat128) X(GNUFloat32) X(GNUFloat64) X(GNUFloat32x) X(GNUFloat64x) \
    X(GNUFloat128x) X(GNUQuad) X(GNUFloat16) X(GNUBFloat16) X(GNUTypeofType) X(GNUTypeofExpression) \
    X(StructDefinition) X(StructReference) X(GNUEmptyStruct) X(GNUAttributedStruct) X(Struct) \
    X(Union) X(GNUAttributedStructKind) X(NoIdent) X(SomeIdent) X(StructDeclaration) \
    X(StructStaticAssertion) X(GNUEmptyStructDeclaration) X(GNUStructDeclarationExtension) \
    X(GNUStructPragma) X(UniqueSpecifierQualifiers) X(PrimitiveSpecifierQualifiers) \
    X(PrefixedSpecifierQualifiers) X(GNUAlignedMemberSpecifiers) X(PrimitiveQualifierTail) \
    X(QualifierTail) X(NoStructDeclarators) X(StructDeclarators) X(StructDeclarator) X(BitField) \
    X(NoDeclarator) X(SomeDeclarator) X(EnumDefinition) X(EnumDefinitionComma) X(EnumReference) \
    X(Enumerator) X(EnumeratorValue) X(GNUEnumAttributes) X(GNUEnumDefinition) \
    X(GNUEnumDefinitionComma) X(GNUEnumReference) X(GNUAttributedEnum) \
    X(Const) X(Restrict) X(Volatile) X(AtomicQualifier) \
    X(GNUConst) X(GNUConstPrefix) X(GNUVolatile) X(GNUVolatilePrefix) X(GNURestrict) \
    X(GNURestrictPrefix) X(GNUAttributeQualifier) X(Inline) X(Noreturn) X(GNUInline) \
    X(GNUInlinePrefix) X(AlignasType) X(AlignasExpression) X(Declarator) X(PointerDeclarator) \
    X(Pointer) X(NestedPointer) X(NoTypeQualifiers) X(SomeTypeQualifiers) X(NameDeclarator) \
    X(ParenthesizedDeclarator) X(ArrayDeclarator) X(StaticArrayDeclarator) \
    X(QualifiedStaticArrayDeclarator) X(VariableArrayDeclarator) X(FunctionDeclarator) \
    X(OldFunctionDeclarator) X(NoIdentifiers) X(Identifiers) X(NoExpression) X(SomeExpression) \
    X(ParameterTypes) X(VariadicParameterTypes) X(NamedParameter) X(AbstractParameter) \
    X(GNUAttributedDeclarator) X(GNUAsmDeclarator) X(GNUAttributedAbstractDeclarator) \
    X(TypeName) X(NoAbstractDeclarator) X(SomeAbstractDeclarator) X(AbstractPointer) \
    X(AbstractDeclarator) X(PointerAbstractDeclarator) X(ParenthesizedAbstractDeclarator) \
    X(AbstractArray) X(NestedAbstractArray) X(AbstractStaticArray) X(NestedAbstractStaticArray) \
    X(AbstractQualifiedStaticArray) X(NestedAbstractQualifiedStaticArray) \
    X(AbstractVariableArray) X(NestedAbstractVariableArray) X(AbstractFunction) \
    X(NestedAbstractFunction) X(NoParameterTypes) X(SomeParameterTypes) \
    X(InitializerExpression) X(InitializerList) X(InitializerListComma) X(GNUEmptyInitializer) \
    X(InitializerElement) X(DesignatedInitializerElement) X(GNUOldArrayInitializer) \
    X(ArrayDesignator) X(MemberDesignator) X(GNUArrayRangeDesignator) \
    X(GNUIdent) X(GNULabelDeclaration) X(GNUAttributedLabelDeclaration) X(GNUCaseDeclaration) \
    X(GNUCaseRangeDeclaration) X(GNUDefaultDeclaration) X(GNUAttributeStatement) \
    X(LabeledStatement) X(CompoundStatement) X(ExpressionStatement) X(SelectionStatement) \
    X(IterationStatement) X(JumpStatement) X(Label) X(GNUAttributedLabel) X(Case) X(Default) X(GNUCaseRange) X(Block) \
    X(BlockDeclaration) X(BlockStatement) X(GNUBlockPragma) X(NoFullExpression) \
    X(SomeFullExpression) X(If) X(IfElse) X(Switch) X(While) X(DoWhile) X(For) X(ForDeclaration) \
    X(Goto) X(GNUIndirectGoto) X(Continue) X(Break) X(Return) X(GNUAsmStatement) X(GNUAsmOutput) \
    X(GNUAsmInput) X(GNUAsmWithClobbers) X(GNUAsmLabels) X(GNUAsmOperand) X(GNUAsmNamedOperand) \
    X(GNUAsmLabel) X(Var) X(IntConst) X(FloatConst) X(CharConst) X(StringLiterals) X(Generic) \
    X(GenericType) X(GenericDefault) X(Subscript) X(Call) X(NoArguments) X(Arguments) X(Member) \
    X(PointerMember) X(PostIncrement) X(PostDecrement) X(CompoundLiteral) X(CompoundLiteralComma) \
    X(GNUEmptyCompoundLiteral) X(PreIncrement) X(PreDecrement) X(AddressOf) X(Indirection) \
    X(UnaryPlus) X(Negate) X(BitNot) X(Not) X(SizeofExpression) X(SizeofType) X(Alignof) X(Cast) \
    X(Multiply) X(Divide) X(Remainder) X(Add) X(Subtract) X(ShiftLeft) X(ShiftRight) X(Less) \
    X(Greater) X(LessEqual) X(GreaterEqual) X(Equal) X(NotEqual) X(BitAnd) X(BitXor) X(BitOr) \
    X(And) X(Or) X(Conditional) X(GNUConditional) X(Assign) X(MultiplyAssign) X(DivideAssign) \
    X(RemainderAssign) X(AddAssign) X(SubtractAssign) X(ShiftLeftAssign) X(ShiftRightAssign) \
    X(BitAndAssign) X(BitXorAssign) X(BitOrAssign) X(Comma) X(GNUStatementExpression) \
    X(GNUExpressionExtension) X(GNUOffsetof) X(GNUOffsetMember) X(GNUOffsetNestedMember) \
    X(GNUOffsetSubscript) X(GNUVaArg) X(GNUCompatibleTypes) X(GNUConvertVector) \
    X(GNUAlignofType) X(GNUAlignofExpression) X(GNULabelAddress) X(GNUAttribute) \
    X(GNUAttributeItem) X(GNUAttributeCall) X(GNUNamedAttribute) X(GNUConstAttribute) \
    X(GNUConstUnderscoresAttribute) X(GNUConstPrefixAttribute) X(GNUAttributeArguments) \
    X(GNUNoAttributeArguments)

enum {
    CE_K_NONE,
#define CE_SEMA_KIND(name) CE_K_##name,
    CE_SEMA_LABELS(CE_SEMA_KIND)
#undef CE_SEMA_KIND
    CE_K_COUNT
};

/* A map from tree nodes (or any pointer) to pointers. */
typedef struct { const void **keys; void **values; size_t len, capacity; } CePointerMap;
void *ce_pointer_get(const CePointerMap *map, const void *key);
void ce_pointer_put(CePointerMap *map, const void *key, void *value);
void ce_pointer_map_free(CePointerMap *map);

/* N1570 §6.2.3 name spaces; members are resolved per record during elaboration. */
enum { CE_SPACE_ORDINARY, CE_SPACE_TAG, CE_SPACE_LABEL };
enum {
    CE_ENTITY_OBJECT, CE_ENTITY_FUNCTION, CE_ENTITY_TYPEDEF, CE_ENTITY_ENUMERATOR,
    CE_ENTITY_PARAMETER, CE_ENTITY_STRUCT, CE_ENTITY_UNION, CE_ENTITY_ENUM, CE_ENTITY_LABEL,
    CE_ENTITY_BUILTIN
};
/* N1570 §6.2.1 scopes and §6.2.2 linkage. */
enum { CE_SCOPE_FILE, CE_SCOPE_BLOCK, CE_SCOPE_PROTOTYPE, CE_SCOPE_FUNCTION };
enum { CE_LINKAGE_NONE, CE_LINKAGE_INTERNAL, CE_LINKAGE_EXTERNAL };

/* ---- the C type language (ctype.c) ---------------------------------------- */

enum {
    CE_T_VOID, CE_T_BOOL, CE_T_CHAR, CE_T_SCHAR, CE_T_UCHAR, CE_T_SHORT, CE_T_USHORT,
    CE_T_INT, CE_T_UINT, CE_T_LONG, CE_T_ULONG, CE_T_LLONG, CE_T_ULLONG, CE_T_INT128,
    CE_T_UINT128, CE_T_FLOAT, CE_T_DOUBLE, CE_T_LDOUBLE, CE_T_FLOAT32, CE_T_FLOAT64,
    CE_T_FLOAT128, CE_T_FLOAT32X, CE_T_FLOAT64X, CE_T_FLOAT16, CE_T_BFLOAT16, CE_T_COMPLEX, CE_T_ENUM, CE_T_POINTER,
    CE_T_ARRAY, CE_T_FUNCTION, CE_T_STRUCT, CE_T_UNION, CE_T_KINDS
};
enum { CE_Q_CONST = 1, CE_Q_VOLATILE = 2, CE_Q_RESTRICT = 4, CE_Q_ATOMIC = 8 };

typedef struct {
    const char *name;
    unsigned char size[CE_T_KINDS], align[CE_T_KINDS];
    unsigned char precision[CE_T_KINDS];
    bool char_signed;
    unsigned size_type, ptrdiff_type, wchar_type, char16_type, char32_type, word_type;
    unsigned max_align;
    unsigned enum_policy;         /* how an enumeration's integer type is chosen */
    unsigned record_layout;       /* target record and bit-field allocation policy */
} CeAbi;
enum { CE_ENUM_GNU, CE_ENUM_INT };
enum { CE_RECORD_SYSV, CE_RECORD_MS };
extern const CeAbi ce_abi_x86_64_sysv, ce_abi_i386_sysv, ce_abi_x86_64_windows;
const CeAbi *ce_abi_named(const char *name);

struct CeEntity;
typedef struct CeType {
    unsigned kind, qualifiers;
    struct CeType *base;          /* pointee, element, return, or complex real type */
    struct CeEntity *tag;         /* struct, union or enum entity */
    struct CeEntity *alias;       /* typedef sugar: the name this type was written as */
    long long length;             /* arrays: element count, -1 incomplete, -2 variable */
    struct CeType **parameters;
    size_t nparameters;
    bool variadic, prototype;
    unsigned alignment;           /* GNU aligned attribute on a typedef */
    unsigned array_qualifiers;    /* qualifiers inside [], for parameter adjustment */
} CeType;

typedef struct {
    CeText name;                  /* empty for unnamed bit-fields and anonymous members */
    CeType *type;
    unsigned long long offset;    /* bytes */
    unsigned bit_offset, bit_width;
    bool bitfield, anonymous;
    const CeTree *declaration;
} CeMember;
typedef struct CeRecord {
    CeMember *members;
    size_t nmembers;
    bool complete, packed, flexible;
    bool attributed;              /* an aligned attribute or _Alignas changed the layout */
    unsigned long long size;
    unsigned align;
    unsigned enum_kind;           /* an enumeration's compatible integer type */
} CeRecord;

struct CeType;
struct CeRecord;
typedef struct CeEntity {
    unsigned kind, space, scope, linkage;
    size_t id;
    CeText name;                  /* normalized key; empty for anonymous tags */
    const CeTree *declaration;    /* name token (or tag specifier) of the first declaration */
    const CeTree *definition;     /* tag definition or function definition, when seen */
    bool defined;
    bool needs_complete;          /* an object definition, including a tentative one */
    bool function_typedef;        /* a typedef whose declarator denotes a function */
    struct CeType *type;          /* set during elaboration */
    struct CeRecord *record;      /* struct, union and enumeration contents */
    long long value;              /* enumeration constant value */
    bool value_known;
    struct CeEntity *naming_alias;/* the typedef that names an anonymous tag */
    unsigned alignment;           /* _Alignas or aligned on an object */
    bool builtin_tag;             /* a target-provided record without a source name */
    struct CeType *witness_record;/* an anonymous tag: a record with a member of this type */
    char *witness_path;           /* ...and how that member reaches an object of it */
    struct CeEntity *witness_object; /* or a file-scope object whose type reaches it */
    struct CeType *anonymous_in;  /* an anonymous member's containing record (§6.7.2.1p13) */
} CeEntity;

/* One identifier occurrence that denotes an entity. */
enum { CE_USE_REFERENCE, CE_USE_DECLARATION, CE_USE_TYPEDEF_NAME, CE_USE_TAG, CE_USE_LABEL,
       CE_USE_ATTRIBUTE };
typedef struct {
    const CeTree *node;           /* identifier token, or tag specifier node */
    CeEntity *entity;
    const CeTree *visible;        /* the visible declaration's name token */
    unsigned role;
    bool folded;                  /* inside an array size or array designator */
} CeUse;

typedef struct CeSema {
    CeArena arena;
    const CeGrammar *grammar;
    CeText source;                /* the parsed text, when the tree came from C */
    unsigned *kinds;              /* production -> CE_K_* */
    CeEntity **entities;
    size_t nentities, entity_capacity;
    CeUse *uses;
    size_t nuses, use_capacity;
    CePointerMap use_of;          /* node -> CeUse index+1 */
    CePointerMap entity_of;       /* declaring name token -> entity */
    CePointerMap tag_of;          /* tag specifier node -> entity */
    const CeAbi *abi;
    bool clang;                   /* Clang's GNU dialect rather than GCC's */
    bool arithmetic_used[128][128];/* usual arithmetic conversions applied, by operand code */
    CeType *basic[CE_T_KINDS];
    CePointerMap exprs;           /* expression node -> CeExpr */
    CePointerMap type_names;      /* TypeName node -> CeType */
    CeType *va_list_type;
    CePointerMap extents;         /* node -> source byte extent */
    size_t unnamed;               /* conversions whose type has no C name */
} CeSema;

/* ---- elaboration (elaborate.c) ------------------------------------------- */

/* Implicit and explicit conversions, named as C compilers name them. */
#define CE_CONVERSIONS(X) \
    X(LValueToRValue) X(ArrayToPointerDecay) X(FunctionToPointerDecay) X(BuiltinFnToFnPtr) \
    X(NoOp) X(BitCast) X(NullToPointer) X(IntegralCast) X(IntegralToBoolean) \
    X(IntegralToFloating) X(IntegralToPointer) X(PointerToIntegral) X(PointerToBoolean) \
    X(FloatingToIntegral) X(FloatingToBoolean) X(FloatingCast) X(ToVoid) X(ToUnion) \
    X(IntegralRealToComplex) X(FloatingRealToComplex) X(FloatingComplexCast) \
    X(FloatingComplexToReal) X(IntegralComplexCast) X(IntegralComplexToReal) \
    X(FloatingComplexToIntegralComplex) X(IntegralComplexToFloatingComplex) \
    X(FloatingComplexToBoolean) X(IntegralComplexToBoolean) X(AtomicToNonAtomic) \
    X(NonAtomicToAtomic) X(LValueBitCast)
enum {
    CE_CONVERSION_NONE,
#define CE_CONVERSION_KIND(name) CE_CONVERT_##name,
    CE_CONVERSIONS(CE_CONVERSION_KIND)
#undef CE_CONVERSION_KIND
    CE_CONVERSION_COUNT
};
extern const char *const ce_conversion_names[];

enum { CE_RVALUE, CE_LVALUE, CE_DESIGNATOR };
typedef struct { unsigned kind; CeType *to; } CeConversion;
typedef struct CeExpr {
    CeType *type;
    unsigned category;
    unsigned bits;                /* the designated bit-field's width */
    unsigned store_bits;          /* initializer stores into a bit-field */
    CeType *store_type;
    bool null_pointer, constant;
    unsigned long long value;     /* an integer constant's bits */
    CeConversion conversions[4];  /* applied by the enclosing context, in order */
    unsigned nconversions;
    unsigned cast;                /* an explicit cast's conversion */
    bool builtin, generic;        /* a builtin function designator; typed by its arguments */
    const CeTree *selected;       /* _Generic's chosen association, or __extension__'s operand */
    const CeTree *member;         /* the member designated by . or -> */
    CeType *computation;          /* a compound assignment's computation type */
    bool no_probe;                /* a string initializing an array, or an asm operand */
    bool folded;                  /* inside an array size or array designator */
} CeExpr;

const CeExpr *ce_sema_expr(const CeSema *sema, const CeTree *node);
bool ce_elaborate(CeSema *sema, const CeTree *unit, CeError *error);

/* CE_PRINT_COMPATIBLE writes an enumeration as its compatible integer type,
   which no declaration can shadow (§6.7.2.2p4). */
enum { CE_PRINT_SUGAR = 1, CE_PRINT_ANYWAY = 2, CE_PRINT_COMPATIBLE = 4, CE_PRINT_DECLARED = 8 };
/* CE_PRINT_DECLARED writes a target-provided record by its declared tag, as
   compilers print it, rather than by an expression that reaches it. */
CeType *ce_type_new(CeSema *sema, unsigned kind);
CeType *ce_type_basic(CeSema *sema, unsigned kind);
CeType *ce_type_copy(CeSema *sema, const CeType *t);
CeType *ce_type_qualified(CeSema *sema, const CeType *t, unsigned qualifiers);
CeType *ce_type_unqualified(CeSema *sema, const CeType *t);
CeType *ce_type_pointer(CeSema *sema, CeType *to);
CeType *ce_type_array(CeSema *sema, CeType *of, long long length);
bool ce_type_integer(const CeType *t);
bool ce_type_real_floating(const CeType *t);
bool ce_type_floating(const CeType *t);
bool ce_type_arithmetic(const CeType *t);
bool ce_type_scalar(const CeType *t);
bool ce_type_record(const CeType *t);
bool ce_type_void(const CeType *t);
unsigned ce_type_integer_kind(const CeType *t);
bool ce_kind_signed(const CeAbi *abi, unsigned kind);
unsigned ce_kind_width(const CeAbi *abi, unsigned kind);
unsigned ce_promoted_kind(const CeAbi *abi, unsigned kind, unsigned bits);
CeType *ce_type_promoted(CeSema *sema, const CeType *t, unsigned bits);
CeType *ce_type_common(CeSema *sema, const CeType *a, const CeType *b, unsigned abits, unsigned bbits);
CeType *ce_type_default_promoted(CeSema *sema, const CeType *t);
bool ce_type_complete(const CeType *t);
unsigned long long ce_type_size(const CeSema *sema, const CeType *t);
unsigned ce_type_align(const CeSema *sema, const CeType *t);
bool ce_type_compatible(CeSema *sema, const CeType *a, const CeType *b);
bool ce_type_same(const CeType *a, const CeType *b);
bool ce_type_similar(const CeType *a, const CeType *b);
CeType *ce_type_composite(CeSema *sema, CeType *a, CeType *b);
char *ce_type_text(CeSema *sema, const CeType *t, unsigned flags);

unsigned ce_sema_kind(const CeSema *sema, const CeTree *tree);
CeText ce_sema_name(CeSema *sema, const CeTree *token);
const CeTree *ce_declarator_name(const CeSema *sema, const CeTree *declarator);
CeEntity *ce_sema_entity(CeSema *sema, unsigned kind, unsigned space, CeText name,
                         const CeTree *declaration);
const CeUse *ce_sema_use(const CeSema *sema, const CeTree *node);
bool ce_bind(CeSema *sema, const CeTree *unit, CeError *error);
bool ce_sema_init(CeSema *sema, const CeGrammar *grammar, CeText source, CeError *error);
bool ce_sema_extents(CeSema *sema, const CeTree *root, const CeTokens *tokens, CeError *error);
void ce_sema_free(CeSema *sema);

#endif
