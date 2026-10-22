/* Generated from the shared .module reader; do not edit. */
#ifndef CETTA_TENSOR_DESCRIPTORS_GENERATED_H
#define CETTA_TENSOR_DESCRIPTORS_GENERATED_H
#define CETTA_TENSOR_DESCRIPTORS_SHA256 "9e13ee6cf98151ca1bc0f64737ec489f2df34bd634502cb72e93d1cd2c95d3e9"
#define CETTA_TENSOR_DTYPE_ROWS(X) \
    X(I64, 1, "i64", 8, Signed, 64) \
    X(F64, 2, "f64", 8, Floating, 53) \
    X(U8, 3, "u8", 1, Unsigned, 8) \
    X(F32, 4, "f32", 4, Floating, 24) \
    X(BOOL, 5, "bool", 1, Boolean, 1) \
    X(INDEX, 6, "index", 8, Unsigned, 64)
#define CETTA_TENSOR_LAYOUT_ROWS(X) \
    X(DENSE, 0, "dense", AllPresent, RowMajorOrStrided) \
    X(COO, 1, "coo", ExplicitSupport, CoordinateAscending) \
    X(CSR, 2, "csr", ExplicitSupport, RowCompressed) \
    X(CSC, 3, "csc", ExplicitSupport, ColumnCompressed)
#define CETTA_TENSOR_OBSERVATION_ROWS(X) \
    X("equality", "DTypeShapeSupportScalarBits") \
    X("sum", "StoredCoordinateOrder|F64AccumulateFloats") \
    X("matvec", "StoredRowCoordinateOrder|F64AccumulateFloats") \
    X("fixed4", "FourLanesLeftAssociated|ExplicitSelection") \
    X("topk", "FiniteEnabledDescending|CoordinateAscendingTies") \
    X("entries", "CoordinateAscending|PackedCoordinateValueVectors") \
    X("gather", "IndexPullbackSupport|BoundsChecked") \
    X("merge", "OccurrenceConcatenate|ExplicitDuplicatePolicy") \
    X("zipmap", "EqualShapeSupport|CapturedValueCall|CoordinateAscending|AtomicFallback|COOResult") \
    X("screen", "OwnerQueryMaskKBound|NearestGradualSequential|InclusiveThreshold|CoordinateAscendingTies|ExactRerank|FullScanFallback")
#define CETTA_TENSOR_EXACT_PROMOTION_ROWS(X) \
    X(I64, I64) \
    X(F64, F64) \
    X(U8, I64) \
    X(U8, F64) \
    X(U8, U8) \
    X(U8, F32) \
    X(U8, INDEX) \
    X(F32, F64) \
    X(F32, F32) \
    X(BOOL, I64) \
    X(BOOL, F64) \
    X(BOOL, U8) \
    X(BOOL, F32) \
    X(BOOL, BOOL) \
    X(BOOL, INDEX) \
    X(INDEX, INDEX)
#define CETTA_TENSOR_CTN1_DTYPE_ROWS(X) \
    X(I64) \
    X(F64) \
    X(U8) \
    X(F32)
#endif
