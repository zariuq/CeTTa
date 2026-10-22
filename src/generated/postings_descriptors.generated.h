/* Generated from the shared .module reader; do not edit. */
#ifndef CETTA_POSTINGS_DESCRIPTORS_GENERATED_H
#define CETTA_POSTINGS_DESCRIPTORS_GENERATED_H
#define CETTA_POSTINGS_DESCRIPTORS_SHA256 "8f10eb59076ffaf2a5cda46c1ebd3e23c6c1d7c6e8658b14670382793e9cd9ba"
#define CETTA_POSTINGS_COUNT_MODE_ROWS(X) \
    X(BEFORE, "before", INDEX, PREFIX, ROWS) \
    X(ALL, "all", INDEX, COMPLETE, ROWS) \
    X(TENSORBEFORE, "tensor-before", INDEX, PREFIX, TENSOR) \
    X(TENSORALL, "tensor-all", INDEX, COMPLETE, TENSOR) \
    X(TABLEBEFORE, "table-before", TABLE, PREFIX, ROWS) \
    X(TABLEALL, "table-all", TABLE, COMPLETE, ROWS) \
    X(TABLETENSORBEFORE, "table-tensor-before", TABLE, PREFIX, TENSOR) \
    X(TABLETENSORALL, "table-tensor-all", TABLE, COMPLETE, TENSOR) \
    X(STATS, "stats", EITHER, METADATA, STATISTICS)
#define CETTA_POSTINGS_OBSERVATION_ROWS(X) \
    X("prepare-index", "SourceOccurrenceOrder|DuplicateOccurrences|NumericPositionsAscending|CompleteValidation|AtomicPublication|OwnedSnapshot|AuthoredFallback") \
    X("prepare-table", "FirstOuterKeyOrder|DuplicateOccurrences|NumericGroupOrder|NumericPositionsAscending|CompleteValidation|AtomicPublication|OwnedSnapshot|AuthoredFallback") \
    X("prepare-space-table-attempt", "BorrowedSource|DeclaredProjection|FirstOuterKeyOrder|DuplicateOccurrences|NumericGroupOrder|NumericPositionsAscending|CompleteValidation|AtomicPublication|OwnedSnapshot|AuthoredFallback") \
    X("inspect", "SourceOccurrenceOrder|DuplicateOccurrences|OwnedSnapshot") \
    X("counts", "DuplicateOccurrences|NumericPositionsAscending|AuthoredFallback") \
    X("sources-current", "ExactSourceRevision|CompleteValidation|AuthoredFallback") \
    X("close", "CloseInvalidatesAliases")
#endif
