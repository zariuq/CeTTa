/* Generated from the shared .module reader; do not edit. */
#ifndef CETTA_POSTINGS_DESCRIPTORS_GENERATED_H
#define CETTA_POSTINGS_DESCRIPTORS_GENERATED_H
#define CETTA_POSTINGS_DESCRIPTORS_SHA256 "c3c7da3cd04bb170a3f9d7b7ddaff0df9dd309b7df991382a1883f572ca8535f"
#define CETTA_POSTINGS_COUNT_MODE_ROWS(X) \
    X(BEFORE, "before", INDEX, PREFIX, ROWS) \
    X(ALL, "all", INDEX, COMPLETE, ROWS) \
    X(TENSORBEFORE, "tensor-before", INDEX, PREFIX, TENSOR) \
    X(TENSORALL, "tensor-all", INDEX, COMPLETE, TENSOR) \
    X(TABLEBEFORE, "table-before", TABLE, PREFIX, ROWS) \
    X(TABLEALL, "table-all", TABLE, COMPLETE, ROWS) \
    X(TABLETENSORBEFORE, "table-tensor-before", TABLE, PREFIX, TENSOR) \
    X(TABLETENSORALL, "table-tensor-all", TABLE, COMPLETE, TENSOR) \
    X(TABLEFOLDBEFORE, "table-fold-before", TABLE, PREFIX, FOLDROWS) \
    X(TABLEFOLDCOLUMNSBEFORE, "table-fold-columns-before", TABLE, PREFIX, FOLDCOLUMNS) \
    X(STATS, "stats", EITHER, METADATA, STATISTICS)
#define CETTA_POSTINGS_OBSERVATION_ROWS(X) \
    X("prepare-index", "SourceOccurrenceOrder|DuplicateOccurrences|NumericPositionsAscending|CompleteValidation|AtomicPublication|OwnedSnapshot|AuthoredFallback") \
    X("prepare-table", "MatchingCompatibleKeys|FirstOuterKeyOrder|DuplicateOccurrences|NumericGroupOrder|NumericPositionsAscending|CompleteValidation|AtomicPublication|OwnedSnapshot|AuthoredFallback") \
    X("prepare-space-table-attempt", "BorrowedSource|DeclaredProjection|MatchingCompatibleKeys|ExactRowWidth|ProtectedStandaloneSource|FirstOuterKeyOrder|DuplicateOccurrences|NumericGroupOrder|NumericPositionsAscending|CompleteValidation|AtomicPublication|OwnedSnapshot|AuthoredFallback") \
    X("inspect", "SourceOccurrenceOrder|DuplicateOccurrences|OwnedSnapshot") \
    X("counts", "DuplicateOccurrences|NumericPositionsAscending|AuthoredFallback") \
    X("fold-table", "QueryOrder|NumericGroupOrder|MatchingCompatibleKeys|ProjectedGroupIdentity|OriginalGroupCallback|IndependentGroupStates|ExpressionState|SingleAnswerNumericState|CompleteValidation|AtomicPublication|AuthoredFallback") \
    X("fold-columns", "QueryOrder|NumericGroupOrder|MatchingCompatibleKeys|ProjectedGroupIdentity|OriginalGroupCallback|IndependentGroupStates|ExpressionState|SingleAnswerNumericState|FirstGroupEncounterOrder|DeclaredLeafDTypes|DenseColumns|CompleteValidation|AtomicPublication|AuthoredFallback") \
    X("sources-current", "ExactSourceRevision|CompleteValidation|AuthoredFallback") \
    X("close", "CloseInvalidatesAliases")
#define CETTA_POSTINGS_BLOCK_LAYOUT_HEAD "Postings:BlockLayout"
#define CETTA_POSTINGS_BLOCK_LAYOUT_LENGTH 8u
#define CETTA_POSTINGS_BLOCK_ROWHEAD 1u
#define CETTA_POSTINGS_BLOCK_ROWWIDTH 2u
#define CETTA_POSTINGS_BLOCK_KEYSFIELD 3u
#define CETTA_POSTINGS_BLOCK_GROUPFIELD 4u
#define CETTA_POSTINGS_BLOCK_POSITIONFIELD 5u
#define CETTA_POSTINGS_BLOCK_PREFIXKEYS 6u
#define CETTA_POSTINGS_BLOCK_FORBIDDENHEADS 7u
#define CETTA_POSTINGS_COLUMNS_HEAD "Postings:Columns"
#define CETTA_POSTINGS_COLUMNS_LENGTH 3u
#define CETTA_POSTINGS_COLUMNS_LABELSCOLUMN 1u
#define CETTA_POSTINGS_COLUMNS_VALUECOLUMNS 2u
#define CETTA_POSTINGS_COLUMN_DTYPE_ROWS(X) \
    X(I64, "i64", false) \
    X(F64, "f64", true)
#endif
