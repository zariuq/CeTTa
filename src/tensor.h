#ifndef CETTA_TENSOR_H
#define CETTA_TENSOR_H

#include "atom.h"
#include "generated/tensor_descriptors.generated.h"

typedef struct CettaTensor CettaTensor;
/* Codes 1--4 retain the CTN1 dense interchange meaning. */
typedef enum {
#define DTYPE(name, code, text, width, family, precision) CETTA_TENSOR_##name = code,
    CETTA_TENSOR_DTYPE_ROWS(DTYPE)
#undef DTYPE
} CettaTensorDType;
enum {
    CETTA_TENSOR_FAMILY_Signed, CETTA_TENSOR_FAMILY_Unsigned,
    CETTA_TENSOR_FAMILY_Floating, CETTA_TENSOR_FAMILY_Boolean
};
enum {
#define DTYPE(name, code, text, width, family, precision) \
    CETTA_TENSOR_WIDTH_##name = width, CETTA_TENSOR_PRECISION_##name = precision, \
    CETTA_TENSOR_FAMILY_##name = CETTA_TENSOR_FAMILY_##family,
    CETTA_TENSOR_DTYPE_ROWS(DTYPE)
#undef DTYPE
};
typedef enum {
#define LAYOUT(name, code, text, support, order) CETTA_TENSOR_##name = code,
    CETTA_TENSOR_LAYOUT_ROWS(LAYOUT)
#undef LAYOUT
} CettaTensorLayout;
typedef enum { CETTA_TENSOR_REJECT, CETTA_TENSOR_FIRST, CETTA_TENSOR_LAST, CETTA_TENSOR_SUM } CettaTensorDuplicates;
typedef enum { CETTA_TENSOR_SEQUENTIAL, CETTA_TENSOR_FIXED4 } CettaTensorSchedule;
typedef enum { CETTA_TENSOR_CAST_EXACT, CETTA_TENSOR_CAST_NEAREST, CETTA_TENSOR_CAST_TRUNCATE } CettaTensorCastMode;
typedef enum {
    CETTA_TENSOR_OK, CETTA_TENSOR_DTYPE, CETTA_TENSOR_SHAPE, CETTA_TENSOR_COUNT,
    CETTA_TENSOR_OVERFLOW, CETTA_TENSOR_NOMEM, CETTA_TENSOR_INDEX_ERROR,
    CETTA_TENSOR_VALUE, CETTA_TENSOR_DUPLICATE, CETTA_TENSOR_ENCODING, CETTA_TENSOR_ZERO_NORM,
    CETTA_TENSOR_INEXACT, CETTA_TENSOR_PROMOTION
} CettaTensorStatus;
typedef union { int64_t i64; uint64_t index; double f64; float f32; uint8_t u8; } CettaTensorScalar;

const char *cetta_tensor_status_name(CettaTensorStatus status);
const char *cetta_tensor_dtype_name(CettaTensorDType dtype);
const char *cetta_tensor_layout_name(CettaTensorLayout layout);
size_t cetta_tensor_itemsize(CettaTensorDType dtype);
/* Logical coordinate cardinality, independent of dense byte feasibility. */
CettaTensorStatus cetta_tensor_coordinate_count(const size_t *shape,size_t rank,size_t *count);
CettaTensorStatus cetta_tensor_byte_count(CettaTensorDType dtype, const size_t *shape,
    size_t rank, size_t *count, size_t *bytes);
/* Constructors copy caller data. Each result has one caller reference. */
CettaTensorStatus cetta_tensor_new(CettaTensorDType dtype, const size_t *shape,
    size_t rank, const void *data, size_t bytes, CettaTensor **out);
/* Internal typed sinks may transfer a malloc-compatible, exclusively owned
 * buffer. Ownership transfers only on success; no mutable alias may survive. */
CettaTensorStatus cetta_tensor_new_take(CettaTensorDType dtype, const size_t *shape,
    size_t rank, void *data, size_t bytes, CettaTensor **out);
CettaTensorStatus cetta_tensor_coo(CettaTensorDType dtype, const size_t *shape,
    size_t rank, const size_t *flat_coordinates, const void *values, size_t entries,
    CettaTensorDuplicates duplicates, CettaTensor **out);
void cetta_tensor_retain(void *tensor);
void cetta_tensor_release(void *tensor);
Atom *cetta_tensor_atom(Arena *arena, CettaTensor *tensor);
CettaTensor *cetta_tensor_from_atom(const Atom *atom);
size_t cetta_tensor_rank(const CettaTensor *tensor);
const size_t *cetta_tensor_shape(const CettaTensor *tensor);
size_t cetta_tensor_count(const CettaTensor *tensor);
size_t cetta_tensor_stored(const CettaTensor *tensor);
size_t cetta_tensor_retained_bytes(const CettaTensor *tensor);
CettaTensorDType cetta_tensor_dtype(const CettaTensor *tensor);
CettaTensorLayout cetta_tensor_layout(const CettaTensor *tensor);
CettaTensorStatus cetta_tensor_get(const CettaTensor *tensor, size_t flat,
    bool *present, CettaTensorScalar *value);
/* Views are immutable element-strided dense views. Negative and zero strides
 * are allowed after checked bounds validation against the retained owner. */
CettaTensorStatus cetta_tensor_view(const CettaTensor *tensor, const size_t *shape,
    const ptrdiff_t *strides, size_t rank, size_t offset, CettaTensor **out);
CettaTensorStatus cetta_tensor_copy(const CettaTensor *tensor, CettaTensor **out);
/* Logical row-order reshape: preserves a contiguous view's selected offset;
 * noncontiguous dense inputs copy, and sparse support keeps flat ordinals. */
CettaTensorStatus cetta_tensor_reshape(const CettaTensor *tensor,const size_t *shape,
    size_t rank,CettaTensor **out);
CettaTensorStatus cetta_tensor_convert(const CettaTensor *tensor, CettaTensorLayout layout,
    CettaTensor **out);
/* Whole-source-type numeric representability, generated from dtype metadata.
 * It is not scalar-bit equality across different dtypes. */
bool cetta_tensor_exact_promotion(CettaTensorDType source,CettaTensorDType target);
/* Explicit conversion preserves support. Exact refuses lost numerical value;
 * nearest permits floating rounding, truncate explicitly permits fractional
 * removal on an integer destination. Integer overflow never wraps. Floating
 * NaN sign/payload bits are widened exactly, or narrowed under the named mode. */
CettaTensorStatus cetta_tensor_cast(const CettaTensor *tensor,CettaTensorDType target,
    CettaTensorCastMode mode,CettaTensor **out);
bool cetta_tensor_equal(const CettaTensor *left, const CettaTensor *right);
int cetta_tensor_compare(const CettaTensor *left, const CettaTensor *right);
uint64_t cetta_tensor_hash(const CettaTensor *tensor);
bool cetta_tensor_all_finite(const CettaTensor *tensor);
/* Stable scaled normalization of a vector or each matrix row. Refuses zero
 * rows/nonfinite inputs. Returns f64, preserving shape and explicit support. */
CettaTensorStatus cetta_tensor_normalize_rows(const CettaTensor *tensor,CettaTensor **out);
/* Stored-coordinate traversal, in ascending logical coordinate order. Missing
 * entries are not implicit floating operations. Fixed4 is an explicit schedule. */
CettaTensorStatus cetta_tensor_sum(const CettaTensor *tensor, CettaTensorSchedule schedule,
    CettaTensorScalar *result);
CettaTensorStatus cetta_tensor_matvec(const CettaTensor *matrix, const CettaTensor *vector,
    CettaTensorSchedule schedule, CettaTensor **out);
/* Sequential scores for a fully present Index vector of requested rows.
 * Request order and duplicate occurrences survive; unsupported packed layouts
 * use ordinary matvec followed by gather without changing the observation. */
CettaTensorStatus cetta_tensor_matvec_rows(const CettaTensor *matrix, const CettaTensor *vector,
    const CettaTensor *rows, CettaTensor **out);
CettaTensorStatus cetta_tensor_top_k(const CettaTensor *scores, const CettaTensor *mask,
    size_t k, CettaTensor **out);
CettaTensorStatus cetta_tensor_encode(const CettaTensor *tensor, unsigned char **bytes, size_t *size);
CettaTensorStatus cetta_tensor_decode(const unsigned char *bytes, size_t size, CettaTensor **out);
/* Copy dense logical values into caller-owned bytes without scalar Atoms.
 * Sparse support is not implicitly erased at an interchange boundary. */
CettaTensorStatus cetta_tensor_copy_values(const CettaTensor *tensor,void *bytes,size_t size);
/* Explicit packed observation of stored coordinates and scalar values, both
 * in ascending logical coordinate order. Neither observer fills absence. */
CettaTensorStatus cetta_tensor_entries(const CettaTensor *tensor,
    CettaTensor **coordinates, CettaTensor **values);
/* Pull back support and values along an Index tensor. Missing indices or
 * missing source coordinates remain absent; present indices must be in bounds. */
CettaTensorStatus cetta_tensor_gather(const CettaTensor *tensor,
    const CettaTensor *indices, CettaTensor **out);
/* Select leading-axis rows by an Index vector. Request positions and duplicate
 * rows survive; absent indices or source coordinates remain absent. Trailing
 * dimensions and scalar bits are unchanged, including zero-width rows. */
CettaTensorStatus cetta_tensor_gather_rows(const CettaTensor *tensor,
    const CettaTensor *rows, CettaTensor **out);
/* Pointwise selection from already evaluated tensors of identical shape.
 * The condition must be Bool and branch dtypes must agree. Missing condition
 * or selected-branch support stays missing; the unselected branch contributes
 * nothing. Copies scalar bits without arithmetic or implicit broadcasting. */
CettaTensorStatus cetta_tensor_where(const CettaTensor *condition,
    const CettaTensor *when_true, const CettaTensor *when_false, CettaTensor **out);
/* Inclusive sum along one axis, in increasing axis position. Each fiber
 * starts with its first present value; missing positions stay missing. The
 * accumulator has the input dtype, including per-step f32 rounding and
 * checked integer overflow. No reassociation or zero filling is implicit. */
CettaTensorStatus cetta_tensor_scan_sum(const CettaTensor *tensor, size_t axis,
    CettaTensor **out);
/* Axis concatenation preserves both occurrence sequences and their support.
 * A permutation lists the old axis used for each new axis, exactly once. */
CettaTensorStatus cetta_tensor_concatenate(const CettaTensor *left,
    const CettaTensor *right, size_t axis, CettaTensor **out);
CettaTensorStatus cetta_tensor_permute(const CettaTensor *tensor,
    const size_t *axes, size_t rank, CettaTensor **out);
/* Concatenate the two stored occurrence sequences, then apply the explicit
 * duplicate policy. Shape and dtype must agree. */
CettaTensorStatus cetta_tensor_merge(const CettaTensor *left,
    const CettaTensor *right, CettaTensorDuplicates duplicates, CettaTensor **out);

#endif
