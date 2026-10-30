profile CGNU;
include "C11.profile";
-- GNU __auto_type keeps the outer name visible throughout the initializer.
scope deferred "GNUAutoType" "UniqueTypeSpecifier";
-- Preprocessing retains these implementation directives as complete lines.
layout token CPragma line;
layout token CIdentDirective line;
-- Target/compiler predefined type names; classified through the same scope hook.
initial typedef "__builtin_va_list";
initial typedef "__builtin_ms_va_list";
initial typedef "__int128_t";
initial typedef "__uint128_t";
-- A following attribute attaches to the immediately preceding struct/declarator.
conflict "__attribute__" prefer shift over reduce "StructType" "UniqueTypeSpecifier";
conflict "__attribute" prefer shift over reduce "StructType" "UniqueTypeSpecifier";
conflict "__attribute__" prefer shift over reduce "EnumType" "UniqueTypeSpecifier";
conflict "__attribute" prefer shift over reduce "EnumType" "UniqueTypeSpecifier";
conflict "__attribute__" prefer shift over reduce "Declarator" "Declarator";
conflict "__attribute" prefer shift over reduce "Declarator" "Declarator";
conflict "__attribute__" prefer shift over reduce "PointerDeclarator" "Declarator";
conflict "__attribute" prefer shift over reduce "PointerDeclarator" "Declarator";
-- The C11 typedef-parameter interpretation extends through GNU attributes.
conflict "__attribute__" prefer reduce "TypedefType" "UniqueTypeSpecifier" over reduce "_" "CGeneralTypedefName";
conflict "__attribute" prefer reduce "TypedefType" "UniqueTypeSpecifier" over reduce "_" "CGeneralTypedefName";
-- Attributes immediately after a named label belong to the label. Absorb the
-- complete run before selecting its statement or declaration body.
conflict any prefer reduce "(:[])" "[GNUAttribute]" over reduce "GNUAttributeQualifier" "TypeQualifier";
conflict any prefer reduce "(:)" "[GNUAttribute]" over reduce "GNUAttributeQualifier" "TypeQualifier";
conflict any prefer reduce "(:)" "[GNUAttribute]" over reduce "(:[])" "[GNUAttribute]";
conflict any prefer reduce "ScopeDeclarationOpen" "ScopeDeclarationOpen" over reduce "GNUAttributeSpecifiers" "DeclarationSpecifiers";
conflict ";" prefer reduce "NoFullExpression" "OptionalFullExpression" over shift;
layout attach ":" in "GNUCaseRange" "LabeledStatement" "GNUAttributedLabel" "LabeledStatement"
    "GNULabelDeclaration" "LabeledStatement" "GNUAttributedLabelDeclaration" "LabeledStatement"
    "GNUCaseDeclaration" "LabeledStatement";
