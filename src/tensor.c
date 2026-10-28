#include "tensor.h"
#include <fenv.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif
_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024, "f64 requires binary64");
_Static_assert(sizeof(float) == 4 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && FLT_RADIX == 2, "f32 requires binary32");
_Static_assert(CETTA_TENSOR_WIDTH_F64==sizeof(double) && CETTA_TENSOR_PRECISION_F64==DBL_MANT_DIG &&
    (int)CETTA_TENSOR_FAMILY_F64==(int)CETTA_TENSOR_FAMILY_Floating,"f64 descriptor does not match its native kernel");
_Static_assert(CETTA_TENSOR_WIDTH_F32==sizeof(float) && CETTA_TENSOR_PRECISION_F32==FLT_MANT_DIG &&
    (int)CETTA_TENSOR_FAMILY_F32==(int)CETTA_TENSOR_FAMILY_Floating,"f32 descriptor does not match its native kernel");
_Static_assert(CETTA_TENSOR_WIDTH_I64==sizeof(int64_t) && CETTA_TENSOR_PRECISION_I64==64 &&
    (int)CETTA_TENSOR_FAMILY_I64==(int)CETTA_TENSOR_FAMILY_Signed,"i64 descriptor does not match its native kernel");
_Static_assert(CETTA_TENSOR_WIDTH_INDEX==sizeof(uint64_t) && CETTA_TENSOR_PRECISION_INDEX==64 &&
    (int)CETTA_TENSOR_FAMILY_INDEX==(int)CETTA_TENSOR_FAMILY_Unsigned,"index descriptor does not match its native kernel");
_Static_assert(CETTA_TENSOR_WIDTH_U8==1 && CETTA_TENSOR_PRECISION_U8==8 &&
    (int)CETTA_TENSOR_FAMILY_U8==(int)CETTA_TENSOR_FAMILY_Unsigned,"u8 descriptor does not match its native kernel");
_Static_assert(CETTA_TENSOR_WIDTH_BOOL==1 && CETTA_TENSOR_PRECISION_BOOL==1 &&
    (int)CETTA_TENSOR_FAMILY_BOOL==(int)CETTA_TENSOR_FAMILY_Boolean,"bool descriptor does not match its native kernel");
_Static_assert(CETTA_TENSOR_I64==1 && CETTA_TENSOR_F64==2 && CETTA_TENSOR_U8==3 && CETTA_TENSOR_F32==4,
    "CTN1 dtype codes require an explicit new transport version to change");

typedef struct {
    _Atomic size_t holds;
    size_t bytes, stored;
    unsigned char *values;
    size_t *coordinates; /* COO only; compressed layouts own segment/minor indexes. */
    bool sparse;
} TensorStorage;

struct CettaTensor {
    const CettaStructuralClass *type;
    _Atomic size_t holds;
    TensorStorage *storage;
    CettaTensorDType dtype;
    CettaTensorLayout layout;
    size_t rank, count, offset, segment_count;
    /* CSC permutation maps canonical row-order entries to physical entries.
     * Values themselves follow the physical compressed layout. */
    size_t *shape, *segments, *permutation, *minor;
    ptrdiff_t *strides;
    _Atomic uint64_t hash;
    _Atomic bool hash_ready;
    _Atomic unsigned finite_state;
    bool contiguous;
};
static const CettaStructuralClass tensor_class;

const char *cetta_tensor_status_name(CettaTensorStatus s) {
    static const char *const names[] = {"ok", "unsupported-dtype", "shape-mismatch", "element-count-mismatch",
        "integer-overflow", "allocation-failure", "index-out-of-bounds", "invalid-value", "duplicate-coordinate", "invalid-encoding", "zero-norm", "inexact-cast", "unsupported-promotion"};
    return (unsigned)s < sizeof(names)/sizeof(*names) ? names[s] : "invalid-status";
}
const char *cetta_tensor_dtype_name(CettaTensorDType d) {
    switch(d) {
#define DTYPE(name, code, text, width, family, precision) case CETTA_TENSOR_##name: return text;
    CETTA_TENSOR_DTYPE_ROWS(DTYPE)
#undef DTYPE
    }
    return "unsupported";
}
const char *cetta_tensor_layout_name(CettaTensorLayout l) {
    switch(l) {
#define LAYOUT(name, code, text, support, order) case CETTA_TENSOR_##name: return text;
    CETTA_TENSOR_LAYOUT_ROWS(LAYOUT)
#undef LAYOUT
    }
    return "unsupported";
}
size_t cetta_tensor_itemsize(CettaTensorDType d) {
    switch (d) {
#define DTYPE(name, code, text, width, family, precision) case CETTA_TENSOR_##name: return width;
    CETTA_TENSOR_DTYPE_ROWS(DTYPE)
#undef DTYPE
    }
    return 0;
}
CettaTensorStatus cetta_tensor_coordinate_count(const size_t *shape,size_t rank,size_t *count) {
    if (!count || (rank && !shape)) return CETTA_TENSOR_VALUE;
    if (rank > SIZE_MAX / sizeof(size_t)) return CETTA_TENSOR_OVERFLOW;
    *count = 1;
    for (size_t i = 0; i < rank; i++) if (!shape[i]) { *count = 0; return CETTA_TENSOR_OK; }
    for (size_t i = 0; i < rank; i++) {
        if (shape[i] > SIZE_MAX / *count) return CETTA_TENSOR_OVERFLOW;
        *count *= shape[i];
    }
    return CETTA_TENSOR_OK;
}
CettaTensorStatus cetta_tensor_byte_count(CettaTensorDType d, const size_t *shape,
    size_t rank, size_t *count, size_t *bytes) {
    size_t width=cetta_tensor_itemsize(d);
    if(!width) return CETTA_TENSOR_DTYPE;
    if(!bytes) return CETTA_TENSOR_VALUE;
    CettaTensorStatus status=cetta_tensor_coordinate_count(shape,rank,count);
    if(status!=CETTA_TENSOR_OK) return status;
    if(*count>SIZE_MAX/width) return CETTA_TENSOR_OVERFLOW;
    *bytes = *count * width;
    return CETTA_TENSOR_OK;
}
static void storage_release(TensorStorage *s) {
    if (s && atomic_fetch_sub_explicit(&s->holds, 1, memory_order_acq_rel) == 1) {
        free(s->values); free(s->coordinates); free(s);
    }
}
static TensorStorage *storage_new(size_t stored, size_t width, bool sparse) {
    if (stored > SIZE_MAX / width || stored > SIZE_MAX / sizeof(size_t)) return NULL;
    TensorStorage *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    atomic_init(&s->holds, 1); s->stored = stored; s->bytes = stored * width; s->sparse = sparse;
    s->values = s->bytes ? malloc(s->bytes) : NULL;
    s->coordinates = sparse && stored ? malloc(stored * sizeof(size_t)) : NULL;
    if ((s->bytes && !s->values) || (sparse && stored && !s->coordinates)) {
        storage_release(s); return NULL;
    }
    return s;
}
void cetta_tensor_retain(void *raw) {
    CettaTensor *t = raw;
    if (t) atomic_fetch_add_explicit(&t->holds, 1, memory_order_relaxed);
}
void cetta_tensor_release(void *raw) {
    CettaTensor *t = raw;
    if (t && atomic_fetch_sub_explicit(&t->holds, 1, memory_order_acq_rel) == 1) {
        storage_release(t->storage); free(t->shape); free(t->strides);
        free(t->segments); free(t->permutation); free(t->minor); free(t);
    }
}
static CettaTensorStatus descriptor(TensorStorage *storage, CettaTensorDType dtype,
    const size_t *shape, size_t rank, size_t count, CettaTensor **out) {
    CettaTensor *t = calloc(1, sizeof(*t));
    if (!t) return CETTA_TENSOR_NOMEM;
    t->shape = rank ? malloc(rank * sizeof(*shape)) : NULL;
    t->strides = rank ? malloc(rank * sizeof(*t->strides)) : NULL;
    if (rank && (!t->shape || !t->strides)) {
        free(t->shape); free(t->strides); free(t); return CETTA_TENSOR_NOMEM;
    }
    t->type = &tensor_class; atomic_init(&t->holds, 1);
    atomic_init(&t->hash,0); atomic_init(&t->hash_ready,false);
    atomic_init(&t->finite_state,0);
    t->storage = storage; atomic_fetch_add_explicit(&storage->holds, 1, memory_order_relaxed);
    t->dtype = dtype; t->layout = storage->sparse ? CETTA_TENSOR_COO : CETTA_TENSOR_DENSE;
    t->rank = rank; t->count = count; t->contiguous = true;
    if (rank) memcpy(t->shape, shape, rank * sizeof(*shape));
    size_t stride = 1;
    for (size_t i = rank; i > 0; i--) {
        t->strides[i-1] = stride <= PTRDIFF_MAX ? (ptrdiff_t)stride : 0;
        if (!shape[i-1] || stride > SIZE_MAX / shape[i-1]) stride = 0;
        else stride *= shape[i-1];
    }
    *out = t; return CETTA_TENSOR_OK;
}
static size_t ordinal(const CettaTensor *t, size_t flat) {
    ptrdiff_t position = (ptrdiff_t)t->offset;
    for (size_t i = t->rank; i > 0; i--) {
        size_t axis = i-1, coordinate = flat % t->shape[axis];
        flat /= t->shape[axis];
        position += (ptrdiff_t)coordinate * t->strides[axis];
    }
    return (size_t)position;
}
static size_t locate(const CettaTensor *t, size_t flat) {
    if (!t->storage->sparse) return t->contiguous ? t->offset + flat : ordinal(t, flat);
    if (t->layout == CETTA_TENSOR_CSR || t->layout == CETTA_TENSOR_CSC) {
        size_t columns = t->shape[1];
        size_t major = t->layout == CETTA_TENSOR_CSR ? flat / columns : flat % columns;
        size_t minor = t->layout == CETTA_TENSOR_CSR ? flat % columns : flat / columns;
        size_t lo = t->segments[major], hi = t->segments[major + 1];
        while (lo < hi) {
            size_t mid = lo + (hi-lo)/2;
            if (t->minor[mid] < minor) lo = mid+1; else hi = mid;
        }
        return lo < t->segments[major+1] && t->minor[lo] == minor ? lo : SIZE_MAX;
    }
    size_t lo = 0, hi = t->storage->stored;
    while (lo < hi) {
        size_t mid = lo + (hi-lo)/2;
        if (t->storage->coordinates[mid] < flat) lo = mid+1; else hi = mid;
    }
    return lo < t->storage->stored && t->storage->coordinates[lo] == flat ? lo : SIZE_MAX;
}
static const unsigned char *element(const CettaTensor *t, size_t flat) {
    size_t i = locate(t, flat);
    return i == SIZE_MAX ? NULL : t->storage->values + i * cetta_tensor_itemsize(t->dtype);
}
static uint64_t hash_word(uint64_t hash, uint64_t word) {
    for (unsigned j = 0; j < 8; j++) { hash ^= (uint8_t)(word >> (8*j)); hash *= UINT64_C(1099511628211); }
    return hash;
}
static uint64_t scalar_bits(const void *p, size_t width) {
    if(width==1) return *(const uint8_t *)p;
    if(width==4) { uint32_t bits; memcpy(&bits,p,4); return bits; }
    uint64_t bits; memcpy(&bits,p,8); return bits;
}
static void scalar_from_bits(void *p,size_t width,uint64_t bits) {
    if(width==1) { *(uint8_t *)p=(uint8_t)bits; }
    else if(width==4) { uint32_t narrow=(uint32_t)bits; memcpy(p,&narrow,4); }
    else memcpy(p,&bits,8);
}
static size_t stored_coordinate(const CettaTensor *t, size_t i) {
    if (!t->storage->sparse) return i;
    if (t->layout == CETTA_TENSOR_COO) return t->storage->coordinates[i];
    size_t physical = t->permutation ? t->permutation[i] : i;
    size_t lo = 0, hi = t->segment_count;
    while (lo < hi) {
        size_t mid = lo + (hi-lo)/2;
        if (t->segments[mid+1] <= physical) lo = mid+1; else hi = mid;
    }
    return t->layout == CETTA_TENSOR_CSR
        ? lo*t->shape[1] + t->minor[physical]
        : t->minor[physical]*t->shape[1] + lo;
}
/* Every semantic observer consumes canonical logical order. The physical
 * column order of CSC must not silently become a reduction schedule. */
static const unsigned char *stored_value(const CettaTensor *t, size_t i) {
    if (!t->storage->sparse) return element(t, i);
    size_t physical = t->permutation ? t->permutation[i] : i;
    return t->storage->values + physical*cetta_tensor_itemsize(t->dtype);
}

CettaTensorStatus cetta_tensor_entries(const CettaTensor *t,
    CettaTensor **coordinates, CettaTensor **values) {
    if (!t || !coordinates || !values || coordinates == values)
        return CETTA_TENSOR_VALUE;
    *coordinates = NULL; *values = NULL;
    size_t n = cetta_tensor_stored(t), width = cetta_tensor_itemsize(t->dtype);
    if (n > SIZE_MAX / sizeof(uint64_t) || n > SIZE_MAX / width)
        return CETTA_TENSOR_OVERFLOW;
    uint64_t *keys = n ? malloc(n * sizeof(*keys)) : NULL;
    unsigned char *data = n ? malloc(n * width) : NULL;
    if (n && (!keys || !data)) { free(keys); free(data); return CETTA_TENSOR_NOMEM; }
    for (size_t i = 0; i < n; i++) {
        size_t coordinate = stored_coordinate(t, i);
        keys[i] = coordinate;
        const void *value = stored_value(t, i);
        memcpy(data + i * width, value, width);
    }
    CettaTensorStatus status = cetta_tensor_new_take(
        CETTA_TENSOR_INDEX, &n, 1, keys, n * sizeof(*keys), coordinates);
    if (status != CETTA_TENSOR_OK) { free(keys); free(data); return status; }
    status = cetta_tensor_new_take(t->dtype, &n, 1, data, n * width, values);
    if (status != CETTA_TENSOR_OK) {
        free(data); cetta_tensor_release(*coordinates); *coordinates = NULL;
    }
    return status;
}

CettaTensorStatus cetta_tensor_gather(const CettaTensor *t,
    const CettaTensor *indices, CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!t || !indices) return CETTA_TENSOR_VALUE;
    if (indices->dtype != CETTA_TENSOR_INDEX) return CETTA_TENSOR_DTYPE;
    size_t n = cetta_tensor_stored(indices), width = cetta_tensor_itemsize(t->dtype);
    if (n > SIZE_MAX / sizeof(size_t) || n > SIZE_MAX / width)
        return CETTA_TENSOR_OVERFLOW;
    size_t *coordinates = n ? malloc(n * sizeof(*coordinates)) : NULL;
    unsigned char *data = n ? malloc(n * width) : NULL;
    if (n && (!coordinates || !data)) {
        free(coordinates); free(data); return CETTA_TENSOR_NOMEM;
    }
    size_t used = 0;
    CettaTensorStatus status = CETTA_TENSOR_OK;
    for (size_t i = 0; i < n; i++) {
        size_t coordinate = stored_coordinate(indices, i);
        uint64_t key; memcpy(&key, element(indices, coordinate), sizeof(key));
        if (key >= t->count) { status = CETTA_TENSOR_INDEX_ERROR; break; }
        const unsigned char *value = element(t, (size_t)key);
        if (!value) continue;
        coordinates[used] = coordinate;
        memcpy(data + used * width, value, width); used++;
    }
    if (status == CETTA_TENSOR_OK && used == indices->count)
        status = cetta_tensor_new(t->dtype, indices->shape, indices->rank,
                                 data, used * width, out);
    else if (status == CETTA_TENSOR_OK)
        status = cetta_tensor_coo(t->dtype, indices->shape, indices->rank,
                                 coordinates, data, used, CETTA_TENSOR_REJECT, out);
    free(coordinates); free(data); return status;
}

static size_t stored_lower_bound(const CettaTensor *t, size_t coordinate) {
    if (!t->storage->sparse) return coordinate < t->count ? coordinate : t->count;
    size_t lo = 0, hi = cetta_tensor_stored(t);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (stored_coordinate(t, mid) < coordinate) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

CettaTensorStatus cetta_tensor_gather_rows(const CettaTensor *t,
    const CettaTensor *rows, CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!t || !rows) return CETTA_TENSOR_VALUE;
    if (rows->dtype != CETTA_TENSOR_INDEX) return CETTA_TENSOR_DTYPE;
    if (!t->rank || rows->rank != 1) return CETTA_TENSOR_SHAPE;
    size_t requested = cetta_tensor_stored(rows), width = cetta_tensor_itemsize(t->dtype);
    /* Bounds still matter when the selected rows contain no coordinates. */
    for (size_t i = 0; i < requested; i++) {
        uint64_t row; memcpy(&row, stored_value(rows, i), sizeof(row));
        if (row >= t->shape[0]) return CETTA_TENSOR_INDEX_ERROR;
    }
    size_t *shape = malloc(t->rank * sizeof(*shape));
    if (!shape) return CETTA_TENSOR_NOMEM;
    memcpy(shape, t->shape, t->rank * sizeof(*shape)); shape[0] = rows->count;
    size_t count;
    CettaTensorStatus status = cetta_tensor_coordinate_count(shape, t->rank, &count);
    if (status != CETTA_TENSOR_OK) { free(shape); return status; }
    if (!count) {
        status = cetta_tensor_new(t->dtype, shape, t->rank, NULL, 0, out);
        free(shape); return status;
    }
    size_t row_width = count / rows->count, used = 0;
    for (size_t i = 0; i < requested; i++) {
        uint64_t row; memcpy(&row, stored_value(rows, i), sizeof(row));
        size_t first = stored_lower_bound(t, (size_t)row * row_width);
        size_t last = stored_lower_bound(t, ((size_t)row + 1) * row_width);
        if (last - first > SIZE_MAX - used) { free(shape); return CETTA_TENSOR_OVERFLOW; }
        used += last - first;
    }
    bool dense = used == count;
    if (used > SIZE_MAX / width || (!dense && used > SIZE_MAX / sizeof(size_t))) {
        free(shape); return CETTA_TENSOR_OVERFLOW;
    }
    unsigned char *data = used ? malloc(used * width) : NULL;
    size_t *coordinates = !dense && used ? malloc(used * sizeof(*coordinates)) : NULL;
    if (used && (!data || (!dense && !coordinates))) {
        free(shape); free(data); free(coordinates); return CETTA_TENSOR_NOMEM;
    }
    size_t written = 0;
    for (size_t i = 0; i < requested; i++) {
        uint64_t row; memcpy(&row, stored_value(rows, i), sizeof(row));
        size_t first = stored_lower_bound(t, (size_t)row * row_width);
        size_t last = stored_lower_bound(t, ((size_t)row + 1) * row_width);
        size_t target = stored_coordinate(rows, i) * row_width;
        for (size_t entry = first; entry < last; entry++) {
            if (!dense) coordinates[written] = target + stored_coordinate(t, entry) - (size_t)row * row_width;
            memcpy(data + written * width, stored_value(t, entry), width); written++;
        }
    }
    if (dense) {
        status = cetta_tensor_new_take(t->dtype, shape, t->rank, data, used * width, out);
        if (status == CETTA_TENSOR_OK) data = NULL;
    } else status = cetta_tensor_coo(t->dtype, shape, t->rank, coordinates, data, used, CETTA_TENSOR_REJECT, out);
    free(shape); free(data); free(coordinates); return status;
}

CettaTensorStatus cetta_tensor_merge(const CettaTensor *left,
    const CettaTensor *right, CettaTensorDuplicates duplicates, CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!left || !right) return CETTA_TENSOR_VALUE;
    if (left->dtype != right->dtype) return CETTA_TENSOR_DTYPE;
    if (left->rank != right->rank ||
        (left->rank && memcmp(left->shape, right->shape, left->rank * sizeof(size_t))))
        return CETTA_TENSOR_SHAPE;
    size_t n = cetta_tensor_stored(left), m = cetta_tensor_stored(right);
    size_t width = cetta_tensor_itemsize(left->dtype);
    if (n > SIZE_MAX - m || n + m > SIZE_MAX / sizeof(size_t) || n + m > SIZE_MAX / width)
        return CETTA_TENSOR_OVERFLOW;
    size_t total = n + m, used = 0;
    size_t *coordinates = total ? malloc(total * sizeof(*coordinates)) : NULL;
    unsigned char *data = total ? malloc(total * width) : NULL;
    if (total && (!coordinates || !data)) {
        free(coordinates); free(data); return CETTA_TENSOR_NOMEM;
    }
    const CettaTensor *inputs[] = {left, right};
    for (size_t j = 0; j < 2; j++) {
        const CettaTensor *input = inputs[j];
        for (size_t i = 0; i < cetta_tensor_stored(input); i++) {
            size_t coordinate = stored_coordinate(input, i);
            coordinates[used] = coordinate;
            memcpy(data + used * width, element(input, coordinate), width); used++;
        }
    }
    CettaTensorStatus status = cetta_tensor_coo(left->dtype, left->shape, left->rank,
        coordinates, data, used, duplicates, out);
    free(coordinates); free(data); return status;
}
static uint64_t calculate_hash(const CettaTensor *t) {
    uint64_t h = hash_word(UINT64_C(14695981039346656037), t->dtype);
    h = hash_word(h, t->rank);
    for (size_t i = 0; i < t->rank; i++) h = hash_word(h, t->shape[i]);
    size_t width = cetta_tensor_itemsize(t->dtype), n = cetta_tensor_stored(t);
    h = hash_word(h, n);
    for (size_t i = 0; i < n; i++) {
        size_t coordinate = stored_coordinate(t, i);
        h = hash_word(h, coordinate);
        const unsigned char *p = stored_value(t, i);
        h = hash_word(h, scalar_bits(p,width));
    }
    return h;
}
static void finish(CettaTensor *t) {
    /* Publication does not require a full semantic hash. Immutable storage
     * makes lazy, thread-safe memoization valid when that observer is used. */
    atomic_store_explicit(&t->hash_ready,false,memory_order_relaxed);
    atomic_store_explicit(&t->finite_state,0,memory_order_relaxed);
}
CettaTensorStatus cetta_tensor_new(CettaTensorDType dtype, const size_t *shape,
    size_t rank, const void *data, size_t bytes, CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    size_t count = 0, expected = 0;
    CettaTensorStatus s = cetta_tensor_byte_count(dtype, shape, rank, &count, &expected);
    if (s != CETTA_TENSOR_OK) return s;
    if (bytes != expected || (bytes && !data)) return CETTA_TENSOR_COUNT;
    if (count > PTRDIFF_MAX) return CETTA_TENSOR_OVERFLOW;
    if (dtype == CETTA_TENSOR_BOOL)
        for (size_t i = 0; i < count; i++) if (((const uint8_t *)data)[i] > 1) return CETTA_TENSOR_VALUE;
    TensorStorage *storage = storage_new(count, cetta_tensor_itemsize(dtype), false);
    if (!storage) return CETTA_TENSOR_NOMEM;
    if (bytes) memcpy(storage->values, data, bytes);
    s = descriptor(storage, dtype, shape, rank, count, out); storage_release(storage);
    if (s == CETTA_TENSOR_OK) finish(*out);
    return s;
}
typedef struct { size_t coordinate, occurrence; } COOEntry;
/* Like the copying constructor, except that a typed sink already owns the
 * unpublished buffer. No Atom or external mutable view is retained. */
CettaTensorStatus cetta_tensor_new_take(CettaTensorDType dtype,const size_t *shape,
    size_t rank,void *data,size_t bytes,CettaTensor **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL; size_t count,expected;
    CettaTensorStatus s=cetta_tensor_byte_count(dtype,shape,rank,&count,&expected);
    if(s!=CETTA_TENSOR_OK) return s;
    if(bytes!=expected || (bytes && !data)) return CETTA_TENSOR_COUNT;
    if(count>PTRDIFF_MAX) return CETTA_TENSOR_OVERFLOW;
    if(dtype==CETTA_TENSOR_BOOL)
        for(size_t i=0;i<count;i++) if(((const uint8_t *)data)[i]>1) return CETTA_TENSOR_VALUE;
    TensorStorage *storage=storage_new(0,cetta_tensor_itemsize(dtype),false);
    if(!storage) return CETTA_TENSOR_NOMEM;
    s=descriptor(storage,dtype,shape,rank,count,out);
    if(s==CETTA_TENSOR_OK) {
        storage->stored=count; storage->bytes=bytes; storage->values=data;
        finish(*out);
    }
    storage_release(storage); return s;
}

static int entry_compare(const void *a, const void *b) {
    const COOEntry *x = a, *y = b;
    if (x->coordinate != y->coordinate) return x->coordinate < y->coordinate ? -1 : 1;
    return x->occurrence < y->occurrence ? -1 : x->occurrence != y->occurrence;
}
static CettaTensorStatus scalar_add(CettaTensorDType d, void *a, const void *b) {
    switch (d) {
    case CETTA_TENSOR_I64: { int64_t x, y, z; memcpy(&x,a,8); memcpy(&y,b,8);
        if (__builtin_add_overflow(x,y,&z)) return CETTA_TENSOR_OVERFLOW;
        memcpy(a,&z,8); break; }
    case CETTA_TENSOR_INDEX: { uint64_t x,y,z; memcpy(&x,a,8); memcpy(&y,b,8);
        if (__builtin_add_overflow(x,y,&z)) return CETTA_TENSOR_OVERFLOW;
        memcpy(a,&z,8); break; }
    case CETTA_TENSOR_U8: { unsigned x=*(uint8_t *)a+*(const uint8_t *)b;
        if (x > UINT8_MAX) return CETTA_TENSOR_OVERFLOW;
        *(uint8_t *)a=(uint8_t)x; break; }
    case CETTA_TENSOR_F32: { float x,y; memcpy(&x,a,4); memcpy(&y,b,4); x+=y; memcpy(a,&x,4); break; }
    case CETTA_TENSOR_F64: { double x,y; memcpy(&x,a,8); memcpy(&y,b,8); x+=y; memcpy(a,&x,8); break; }
    case CETTA_TENSOR_BOOL: return CETTA_TENSOR_DTYPE;
    }
    return CETTA_TENSOR_OK;
}
CettaTensorStatus cetta_tensor_coo(CettaTensorDType dtype, const size_t *shape,
    size_t rank, const size_t *coordinates, const void *values, size_t entries,
    CettaTensorDuplicates duplicates, CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    size_t count, width = cetta_tensor_itemsize(dtype);
    if(!width) return CETTA_TENSOR_DTYPE;
    CettaTensorStatus s = cetta_tensor_coordinate_count(shape,rank,&count);
    if (s != CETTA_TENSOR_OK) return s;
    if ((unsigned)duplicates > CETTA_TENSOR_SUM || (entries && (!coordinates || !values))) return CETTA_TENSOR_VALUE;
    if (entries > SIZE_MAX / sizeof(COOEntry) || entries > SIZE_MAX / width) return CETTA_TENSOR_OVERFLOW;
    bool ordered=true;
    for (size_t i=0;i<entries;i++) {
        if (coordinates[i] >= count || (dtype == CETTA_TENSOR_BOOL && ((const uint8_t *)values)[i] > 1)) {
            return coordinates[i] >= count ? CETTA_TENSOR_INDEX_ERROR : CETTA_TENSOR_VALUE;
        }
        if(i && coordinates[i]<coordinates[i-1]) ordered=false;
    }
    /* Canonical/row-ordered ingress needs no sort or occurrence permutation.
     * Otherwise the occurrence tie-break preserves First/Last/ordered Sum. */
    COOEntry *order = !ordered && entries ? malloc(entries*sizeof(*order)) : NULL;
    if (!ordered && entries && !order) return CETTA_TENSOR_NOMEM;
    if(!ordered) {
        for(size_t i=0;i<entries;i++) order[i]=(COOEntry){coordinates[i],i};
        qsort(order,entries,sizeof(*order),entry_compare);
    }
    size_t unique=0;
    for (size_t i=0;i<entries;i++) {
        if (!i || (ordered ? coordinates[i]!=coordinates[i-1] : order[i].coordinate!=order[i-1].coordinate)) unique++;
        else if (duplicates == CETTA_TENSOR_REJECT) { free(order); return CETTA_TENSOR_DUPLICATE; }
    }
    TensorStorage *storage=storage_new(unique,width,true);
    if (!storage) { free(order); return CETTA_TENSOR_NOMEM; }
    size_t next=0;
    for (size_t i=0;i<entries;i++) {
        size_t coordinate=ordered ? coordinates[i] : order[i].coordinate;
        const unsigned char *value=(const unsigned char *)values+(ordered ? i : order[i].occurrence)*width;
        if (!i || coordinate!=storage->coordinates[next-1]) {
            storage->coordinates[next]=coordinate;
            memcpy(storage->values+next*width,value,width); next++;
        } else if (duplicates == CETTA_TENSOR_LAST) memcpy(storage->values+(next-1)*width,value,width);
        else if (duplicates == CETTA_TENSOR_SUM) {
            s=scalar_add(dtype,storage->values+(next-1)*width,value);
            if (s != CETTA_TENSOR_OK) { storage_release(storage); free(order); return s; }
        }
    }
    free(order); s=descriptor(storage,dtype,shape,rank,count,out); storage_release(storage);
    if (s == CETTA_TENSOR_OK) finish(*out);
    return s;
}
size_t cetta_tensor_rank(const CettaTensor *t) { return t ? t->rank : 0; }
size_t cetta_tensor_count(const CettaTensor *t) { return t ? t->count : 0; }
const size_t *cetta_tensor_shape(const CettaTensor *t) { return t ? t->shape : NULL; }
size_t cetta_tensor_stored(const CettaTensor *t) { return t ? (t->storage->sparse ? t->storage->stored : t->count) : 0; }
CettaTensorDType cetta_tensor_dtype(const CettaTensor *t) { return t ? t->dtype : 0; }
CettaTensorLayout cetta_tensor_layout(const CettaTensor *t) { return t ? t->layout : CETTA_TENSOR_DENSE; }
size_t cetta_tensor_retained_bytes(const CettaTensor *t) {
    if (!t) return 0;
    size_t bytes = sizeof(*t) + sizeof(*t->storage), charge;
#define CHARGE(n,w) do { if (__builtin_mul_overflow((size_t)(n),(size_t)(w),&charge) || __builtin_add_overflow(bytes,charge,&bytes)) return SIZE_MAX; } while(0)
    CHARGE(t->rank,sizeof(size_t)+sizeof(ptrdiff_t)); CHARGE(t->storage->bytes,1);
    if (t->storage->coordinates) CHARGE(t->storage->stored,sizeof(size_t));
    if (t->segments) CHARGE(t->segment_count+1,sizeof(size_t));
    if (t->permutation) CHARGE(t->storage->stored,sizeof(size_t));
    if (t->minor) CHARGE(t->storage->stored,sizeof(size_t));
#undef CHARGE
    return bytes;
}
CettaTensorStatus cetta_tensor_get(const CettaTensor *t,size_t flat,bool *present,CettaTensorScalar *value) {
    if (!t || !present || !value) return CETTA_TENSOR_VALUE;
    if (flat >= t->count) return CETTA_TENSOR_INDEX_ERROR;
    const void *p=element(t,flat); *present=p != NULL; memset(value,0,sizeof(*value));
    if (p) memcpy(value,p,cetta_tensor_itemsize(t->dtype));
    return CETTA_TENSOR_OK;
}
CettaTensorStatus cetta_tensor_view(const CettaTensor *t,const size_t *shape,
    const ptrdiff_t *strides,size_t rank,size_t offset,CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out=NULL;
    if (!t || (rank && !strides)) return CETTA_TENSOR_VALUE;
    if (t->storage->sparse) return CETTA_TENSOR_SHAPE;
    size_t count,bytes;
    CettaTensorStatus s=cetta_tensor_byte_count(t->dtype,shape,rank,&count,&bytes);
    if (s != CETTA_TENSOR_OK) return s;
    if (offset > PTRDIFF_MAX) return CETTA_TENSOR_OVERFLOW;
    ptrdiff_t lo=(ptrdiff_t)offset,hi=lo;
    if (count) for (size_t i=0;i<rank;i++) {
        ptrdiff_t span;
        if (shape[i]-1 > PTRDIFF_MAX || __builtin_mul_overflow((ptrdiff_t)(shape[i]-1),strides[i],&span) ||
            (span < 0 ? __builtin_add_overflow(lo,span,&lo) : __builtin_add_overflow(hi,span,&hi))) return CETTA_TENSOR_OVERFLOW;
    }
    if (lo < 0 || (count ? (size_t)hi >= t->storage->stored : offset > t->storage->stored)) return CETTA_TENSOR_INDEX_ERROR;
    s=descriptor(t->storage,t->dtype,shape,rank,count,out);
    if (s == CETTA_TENSOR_OK) {
        CettaTensor *v=*out; v->offset=offset;
        if(rank) memcpy(v->strides,strides,rank*sizeof(*strides));
        /* Compare to row-major strides of the new shape, not the source. */
        size_t step=1; v->contiguous=true;
        for(size_t i=rank;i>0;i--) {
            if(shape[i-1]>1 && (step>PTRDIFF_MAX || strides[i-1]!=(ptrdiff_t)step)) v->contiguous=false;
            if(shape[i-1] && step<=SIZE_MAX/shape[i-1]) step*=shape[i-1]; else step=0;
        }
        finish(v);
    }
    return s;
}
CettaTensorStatus cetta_tensor_copy(const CettaTensor *t,CettaTensor **out) {
    if (!t || !out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (t->storage->sparse) {
        size_t n=t->storage->stored, width=cetta_tensor_itemsize(t->dtype);
        TensorStorage *storage=storage_new(n,width,true);
        if(!storage) return CETTA_TENSOR_NOMEM;
        for(size_t i=0;i<n;i++) {
            storage->coordinates[i]=stored_coordinate(t,i);
            memcpy(storage->values+i*width,stored_value(t,i),width);
        }
        CettaTensorStatus status=descriptor(storage,t->dtype,t->shape,t->rank,t->count,out);
        storage_release(storage);
        return status;
    }
    size_t width=cetta_tensor_itemsize(t->dtype),bytes=t->count*width;
    unsigned char *data=bytes ? malloc(bytes) : NULL;
    if (bytes && !data) return CETTA_TENSOR_NOMEM;
    for (size_t i=0;i<t->count;i++) memcpy(data+i*width,element(t,i),width);
    CettaTensorStatus s=cetta_tensor_new_take(t->dtype,t->shape,t->rank,data,bytes,out);
    if(s!=CETTA_TENSOR_OK) free(data);
    return s;
}
CettaTensorStatus cetta_tensor_reshape(const CettaTensor *t,const size_t *shape,size_t rank,CettaTensor **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL;
    if(!t) return CETTA_TENSOR_VALUE;
    size_t count=0; CettaTensorStatus status=cetta_tensor_coordinate_count(shape,rank,&count);
    if(status!=CETTA_TENSOR_OK) return status;
    if(count!=t->count) return CETTA_TENSOR_SHAPE;
    if(t->layout==CETTA_TENSOR_CSR || t->layout==CETTA_TENSOR_CSC) {
        CettaTensor *copy=NULL;
        status=cetta_tensor_copy(t,&copy);
        if(status==CETTA_TENSOR_OK) status=cetta_tensor_reshape(copy,shape,rank,out);
        cetta_tensor_release(copy); return status;
    }
    if(!t->storage->sparse && !t->contiguous) {
        CettaTensor *copy=NULL; status=cetta_tensor_copy(t,&copy);
        if(status==CETTA_TENSOR_OK) status=cetta_tensor_reshape(copy,shape,rank,out);
        cetta_tensor_release(copy); return status;
    }
    status=descriptor(t->storage,t->dtype,shape,rank,count,out);
    if(status==CETTA_TENSOR_OK) { (*out)->offset=t->offset; finish(*out); }
    return status;
}
CettaTensorStatus cetta_tensor_convert(const CettaTensor *t,CettaTensorLayout layout,CettaTensor **out) {
    if (!t || !out) return CETTA_TENSOR_VALUE;
    *out=NULL;
    if ((unsigned)layout > CETTA_TENSOR_CSC) return CETTA_TENSOR_VALUE;
    if(layout==t->layout && (layout!=CETTA_TENSOR_DENSE || t->contiguous)) {
        cetta_tensor_retain((void *)t); *out=(CettaTensor *)t; return CETTA_TENSOR_OK;
    }
    if (layout == CETTA_TENSOR_DENSE) {
        /* Layout conversion cannot fill holes. */
        if (cetta_tensor_stored(t) != t->count) return CETTA_TENSOR_SHAPE;
        if (!t->storage->sparse) return cetta_tensor_copy(t,out);
        if(t->count>PTRDIFF_MAX) return CETTA_TENSOR_OVERFLOW;
        size_t width=cetta_tensor_itemsize(t->dtype);
        TensorStorage *storage=storage_new(t->count,width,false);
        if(!storage) return CETTA_TENSOR_NOMEM;
        for(size_t i=0;i<t->count;i++)
            memcpy(storage->values+i*width,stored_value(t,i),width);
        CettaTensorStatus status=descriptor(storage,t->dtype,t->shape,t->rank,t->count,out);
        storage_release(storage); return status;
    }
    if (layout != CETTA_TENSOR_COO && t->rank != 2) return CETTA_TENSOR_SHAPE;
    CettaTensorStatus s;
    if (layout == CETTA_TENSOR_COO) {
        if(t->storage->sparse) return cetta_tensor_copy(t,out);
        if(t->count>SIZE_MAX/sizeof(size_t)) return CETTA_TENSOR_OVERFLOW;
        size_t width=cetta_tensor_itemsize(t->dtype), *coords=t->count ? malloc(t->count*sizeof(size_t)) : NULL;
        unsigned char *values=t->count ? malloc(t->count*width) : NULL;
        if (t->count && (!coords || !values)) { free(coords); free(values); return CETTA_TENSOR_NOMEM; }
        for (size_t i=0;i<t->count;i++) { coords[i]=i; memcpy(values+i*width,element(t,i),width); }
        s=cetta_tensor_coo(t->dtype,t->shape,t->rank,coords,values,t->count,CETTA_TENSOR_REJECT,out);
        free(coords); free(values);
        return s;
    }
    size_t stored=cetta_tensor_stored(t), width=cetta_tensor_itemsize(t->dtype);
    TensorStorage *storage=storage_new(stored,width,false);
    if(!storage) return CETTA_TENSOR_NOMEM;
    storage->sparse=true;
    s=descriptor(storage,t->dtype,t->shape,t->rank,t->count,out);
    storage_release(storage);
    if (s != CETTA_TENSOR_OK) return s;
    CettaTensor *v=*out; v->layout=layout;
    v->segment_count=t->shape[layout == CETTA_TENSOR_CSR ? 0 : 1];
    if (v->segment_count >= SIZE_MAX/sizeof(size_t)) s=CETTA_TENSOR_OVERFLOW;
    else {
        v->segments=calloc(v->segment_count+1,sizeof(size_t));
        v->permutation=layout==CETTA_TENSOR_CSC && v->storage->stored ? malloc(v->storage->stored*sizeof(size_t)) : NULL;
        v->minor=v->storage->stored ? malloc(v->storage->stored*sizeof(size_t)) : NULL;
        if (!v->segments || (v->storage->stored && (!v->minor || (layout==CETTA_TENSOR_CSC && !v->permutation)))) s=CETTA_TENSOR_NOMEM;
        else {
            size_t columns=v->shape[1];
            for(size_t i=0;i<v->storage->stored;i++) {
                size_t flat=stored_coordinate(t,i), group=layout == CETTA_TENSOR_CSR ? flat/columns : flat%columns;
                v->segments[group+1]++;
            }
            for(size_t i=0;i<v->segment_count;i++) v->segments[i+1]+=v->segments[i];
            size_t *next=v->segment_count ? malloc(v->segment_count*sizeof(size_t)) : NULL;
            if(v->segment_count && !next) s=CETTA_TENSOR_NOMEM;
            else {
                if(v->segment_count) memcpy(next,v->segments,v->segment_count*sizeof(size_t));
                for(size_t i=0;i<v->storage->stored;i++) {
                    size_t flat=stored_coordinate(t,i),group=layout == CETTA_TENSOR_CSR ? flat/columns : flat%columns;
                    size_t target=next[group]++;
                    if(v->permutation) v->permutation[i]=target;
                    v->minor[target]=layout==CETTA_TENSOR_CSR ? flat%columns : flat/columns;
                    memcpy(v->storage->values+target*width,stored_value(t,i),width);
                }
                free(next);
            }
        }
    }
    if(s != CETTA_TENSOR_OK) { cetta_tensor_release(v); *out=NULL; }
    return s;
}
int cetta_tensor_compare(const CettaTensor *a,const CettaTensor *b) {
    if(a==b) return 0;
    if(!a || !b) return a ? 1 : -1;
#define CMP(x,y) do { if((x)!=(y)) return (x)<(y) ? -1 : 1; } while(0)
    CMP(a->dtype,b->dtype); CMP(a->rank,b->rank);
    for(size_t i=0;i<a->rank;i++) { CMP(a->shape[i],b->shape[i]); }
    size_t n=cetta_tensor_stored(a),m=cetta_tensor_stored(b),width=cetta_tensor_itemsize(a->dtype);
    CMP(n,m);
    if(!a->storage->sparse && !b->storage->sparse && a->contiguous && b->contiguous) {
        int c=n ? memcmp(a->storage->values+a->offset*width,b->storage->values+b->offset*width,n*width) : 0;
        return c<0 ? -1 : c>0 ? 1 : 0;
    }
    for(size_t i=0;i<n;i++) {
        size_t x=stored_coordinate(a,i),y=stored_coordinate(b,i); CMP(x,y);
        const void *ap=stored_value(a,i);
        const void *bp=stored_value(b,i);
        int c=memcmp(ap,bp,width); if(c) return c<0 ? -1 : 1;
    }
#undef CMP
    return 0;
}
bool cetta_tensor_equal(const CettaTensor *a,const CettaTensor *b) {
    if(!a || !b) return false;
    if(a==b) return true;
    if(atomic_load_explicit(&a->hash_ready,memory_order_acquire) && atomic_load_explicit(&b->hash_ready,memory_order_acquire) &&
        atomic_load_explicit(&a->hash,memory_order_relaxed)!=atomic_load_explicit(&b->hash,memory_order_relaxed)) return false;
    return cetta_tensor_compare(a,b)==0;
}
uint64_t cetta_tensor_hash(const CettaTensor *t) {
    if(!t) return 0;
    if(atomic_load_explicit(&t->hash_ready,memory_order_acquire)) return atomic_load_explicit(&t->hash,memory_order_relaxed);
    uint64_t h=calculate_hash(t); CettaTensor *cache=(CettaTensor *)t;
    atomic_store_explicit(&cache->hash,h,memory_order_relaxed);
    atomic_store_explicit(&cache->hash_ready,true,memory_order_release); return h;
}
static double read_float(CettaTensorDType d,const void *p) {
    if(!p) return 0;
    if(d==CETTA_TENSOR_F32) { float x; memcpy(&x,p,4); return x; }
    double x; memcpy(&x,p,8); return x;
}
bool cetta_tensor_exact_promotion(CettaTensorDType source,CettaTensorDType target) {
#define PROMOTE(from,to) if(source==CETTA_TENSOR_##from && target==CETTA_TENSOR_##to) return true;
    CETTA_TENSOR_EXACT_PROMOTION_ROWS(PROMOTE)
#undef PROMOTE
    return false;
}
static bool floating(CettaTensorDType dtype) {
    return dtype==CETTA_TENSOR_F32 || dtype==CETTA_TENSOR_F64;
}
static CettaTensorStatus cast_scalar(CettaTensorDType source,const void *input,
    CettaTensorDType target,void *output,CettaTensorCastMode mode) {
    if(source==target) { memcpy(output,input,cetta_tensor_itemsize(source)); return CETTA_TENSOR_OK; }
    bool source_float=floating(source),target_float=floating(target);
    bool negative=false; int64_t signed_value=0; uint64_t integer=0;
    double value=0;
    if(source_float) {
        uint64_t bits=scalar_bits(input,cetta_tensor_itemsize(source));
        uint64_t fraction=source==CETTA_TENSOR_F32 ? bits&UINT64_C(0x7fffff) : bits&UINT64_C(0xfffffffffffff);
        bool nan=source==CETTA_TENSOR_F32 ? ((bits>>23)&255)==255 && fraction : ((bits>>52)&2047)==2047 && fraction;
        if(nan) {
            if(!target_float) return CETTA_TENSOR_VALUE;
            if(source==CETTA_TENSOR_F32) {
                uint64_t widened=((bits>>31)<<63)|UINT64_C(0x7ff0000000000000)|(fraction<<29);
                scalar_from_bits(output,8,widened); return CETTA_TENSOR_OK;
            }
            if(mode==CETTA_TENSOR_CAST_EXACT && (fraction&((UINT64_C(1)<<29)-1))) return CETTA_TENSOR_INEXACT;
            uint32_t payload=(uint32_t)(fraction>>29);
            if(!payload) payload=1;
            scalar_from_bits(output,4,((bits>>63)<<31)|UINT64_C(0x7f800000)|payload); return CETTA_TENSOR_OK;
        }
        value=read_float(source,input);
    } else if(source==CETTA_TENSOR_I64) {
        memcpy(&signed_value,input,8); negative=signed_value<0;
        if(!negative) integer=(uint64_t)signed_value;
    } else if(source==CETTA_TENSOR_INDEX) memcpy(&integer,input,8);
    else if(source==CETTA_TENSOR_U8 || source==CETTA_TENSOR_BOOL) integer=*(const uint8_t *)input;
    else return CETTA_TENSOR_DTYPE;
    if(target_float) {
        if(mode==CETTA_TENSOR_CAST_TRUNCATE) return CETTA_TENSOR_VALUE;
        if(mode==CETTA_TENSOR_CAST_NEAREST && fegetround()!=FE_TONEAREST) return CETTA_TENSOR_VALUE;
        if(!source_float) value=negative ? (double)signed_value : (double)integer;
        if(target==CETTA_TENSOR_F32) {
            if(isfinite(value) && fabs(value)>FLT_MAX) return CETTA_TENSOR_OVERFLOW;
            float compact=(float)value;
            if(mode==CETTA_TENSOR_CAST_EXACT && (double)compact!=value) return CETTA_TENSOR_INEXACT;
            value=compact;
        }
        if(!source_float && mode==CETTA_TENSOR_CAST_EXACT) {
            if(negative) {
                if(value < -0x1p63 || value>=0x1p63 || (int64_t)value!=signed_value) return CETTA_TENSOR_INEXACT;
            } else if(value<0 || value>=0x1p64 || (uint64_t)value!=integer) return CETTA_TENSOR_INEXACT;
        }
        if(target==CETTA_TENSOR_F32) { float compact=(float)value; memcpy(output,&compact,4); }
        else memcpy(output,&value,8);
        return CETTA_TENSOR_OK;
    }
    if(target!=CETTA_TENSOR_BOOL && target!=CETTA_TENSOR_U8 && target!=CETTA_TENSOR_I64 && target!=CETTA_TENSOR_INDEX) return CETTA_TENSOR_DTYPE;
    if(source_float) {
        if(!isfinite(value)) return CETTA_TENSOR_VALUE;
        if(target==CETTA_TENSOR_BOOL) {
            if(value!=0 && value!=1) return CETTA_TENSOR_INEXACT;
            *(uint8_t *)output=(uint8_t)value; return CETTA_TENSOR_OK;
        }
        double integral=trunc(value);
        if(mode!=CETTA_TENSOR_CAST_TRUNCATE && integral!=value) return CETTA_TENSOR_INEXACT;
        if(target==CETTA_TENSOR_I64) {
            if(integral < -0x1p63 || integral>=0x1p63) return CETTA_TENSOR_OVERFLOW;
            int64_t result=(int64_t)integral; memcpy(output,&result,8); return CETTA_TENSOR_OK;
        }
        if(integral<0 || integral>=0x1p64) return CETTA_TENSOR_OVERFLOW;
        integer=(uint64_t)integral;
    }
    if(target==CETTA_TENSOR_I64) {
        if(!negative && integer>INT64_MAX) return CETTA_TENSOR_OVERFLOW;
        int64_t result=negative ? signed_value : (int64_t)integer; memcpy(output,&result,8);
    } else if(target==CETTA_TENSOR_INDEX) {
        if(negative) return CETTA_TENSOR_OVERFLOW;
        memcpy(output,&integer,8);
    } else {
        if(negative || integer>(target==CETTA_TENSOR_BOOL ? 1u : UINT8_MAX)) return target==CETTA_TENSOR_BOOL ? CETTA_TENSOR_INEXACT : CETTA_TENSOR_OVERFLOW;
        *(uint8_t *)output=(uint8_t)integer;
    }
    return CETTA_TENSOR_OK;
}
CettaTensorStatus cetta_tensor_cast(const CettaTensor *t,CettaTensorDType target,
    CettaTensorCastMode mode,CettaTensor **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL;
    if(!t || (unsigned)mode>CETTA_TENSOR_CAST_TRUNCATE) return CETTA_TENSOR_VALUE;
    size_t width=cetta_tensor_itemsize(target),n=cetta_tensor_stored(t);
    if(!width) return CETTA_TENSOR_DTYPE;
    if(t->dtype==target) { cetta_tensor_retain((void *)t); *out=(CettaTensor *)t; return CETTA_TENSOR_OK; }
    if(n>SIZE_MAX/width) return CETTA_TENSOR_OVERFLOW;
    unsigned char *data=n ? malloc(n*width) : NULL;
    if(n && !data) return CETTA_TENSOR_NOMEM;
    CettaTensorStatus status=CETTA_TENSOR_OK;
    for(size_t i=0;i<n;i++) {
        const void *input=stored_value(t,i);
        status=cast_scalar(t->dtype,input,target,data+i*width,mode);
        if(status!=CETTA_TENSOR_OK) break;
    }
    if(status==CETTA_TENSOR_OK) {
        if(!t->storage->sparse) {
            status=cetta_tensor_new_take(target,t->shape,t->rank,data,n*width,out);
            if(status==CETTA_TENSOR_OK) data=NULL;
        } else {
            CettaTensor *coo=NULL;
            size_t *coordinates=n ? malloc(n*sizeof(*coordinates)) : NULL;
            if(n && !coordinates) { free(data); return CETTA_TENSOR_NOMEM; }
            for(size_t i=0;i<n;i++) coordinates[i]=stored_coordinate(t,i);
            status=cetta_tensor_coo(target,t->shape,t->rank,coordinates,data,n,CETTA_TENSOR_REJECT,&coo);
            free(coordinates);
            if(status==CETTA_TENSOR_OK) status=cetta_tensor_convert(coo,t->layout,out);
            cetta_tensor_release(coo);
        }
    }
    free(data); return status;
}
bool cetta_tensor_all_finite(const CettaTensor *t) {
    if(!t) return false;
    if(t->dtype!=CETTA_TENSOR_F32 && t->dtype!=CETTA_TENSOR_F64) return true;
    unsigned known=atomic_load_explicit(&t->finite_state,memory_order_acquire);
    if(known) return known==1;
    size_t n=cetta_tensor_stored(t),width=cetta_tensor_itemsize(t->dtype);
    bool finite=true;
    for(size_t i=0;i<n;i++) {
        const void *p=t->storage->sparse ? t->storage->values+i*width : element(t,i);
        if(!isfinite(read_float(t->dtype,p))) { finite=false; break; }
    }
    atomic_store_explicit(&((CettaTensor *)t)->finite_state,finite ? 1u : 2u,memory_order_release);
    return finite;
}
CettaTensorStatus cetta_tensor_normalize_rows(const CettaTensor *t,CettaTensor **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL;
    if(!t) return CETTA_TENSOR_VALUE;
    if(t->dtype!=CETTA_TENSOR_F32 && t->dtype!=CETTA_TENSOR_F64) return CETTA_TENSOR_DTYPE;
    if(t->rank!=1 && t->rank!=2) return CETTA_TENSOR_SHAPE;
    size_t rows=t->rank==1 ? 1 : t->shape[0],columns=t->rank==1 ? t->count : t->shape[1];
    if(rows>SIZE_MAX/sizeof(double)) return CETTA_TENSOR_OVERFLOW;
    double *scale=rows ? calloc(rows,sizeof(double)) : NULL,*squares=rows ? malloc(rows*sizeof(double)) : NULL;
    if(rows && (!scale || !squares)) { free(scale); free(squares); return CETTA_TENSOR_NOMEM; }
    for(size_t row=0;row<rows;row++) squares[row]=1;
    CettaTensorStatus status=CETTA_TENSOR_OK;
    size_t n=cetta_tensor_stored(t);
    for(size_t i=0;i<n;i++) {
        size_t coordinate=stored_coordinate(t,i),row=coordinate/columns;
        const void *p=stored_value(t,i);
        double x=fabs(read_float(t->dtype,p));
        if(!isfinite(x)) { status=CETTA_TENSOR_VALUE; goto done; }
        if(!x) continue;
        if(scale[row]<x) {
            double ratio=scale[row]/x; squares[row]=1+squares[row]*ratio*ratio; scale[row]=x;
        } else { double ratio=x/scale[row]; squares[row]+=ratio*ratio; }
    }
    for(size_t row=0;row<rows;row++) {
        if(!scale[row]) { status=CETTA_TENSOR_ZERO_NORM; goto done; }
        squares[row]=sqrt(squares[row]);
    }
    TensorStorage *storage=storage_new(n,8,t->storage->sparse);
    if(!storage) { status=CETTA_TENSOR_NOMEM; goto done; }
    for(size_t i=0;i<n;i++) {
        size_t coordinate=stored_coordinate(t,i),row=coordinate/columns;
        const void *p=stored_value(t,i);
        double value=(read_float(t->dtype,p)/scale[row])/squares[row];
        memcpy(storage->values+8*i,&value,8);
        if(storage->sparse) storage->coordinates[i]=coordinate;
    }
    status=descriptor(storage,CETTA_TENSOR_F64,t->shape,t->rank,t->count,out); storage_release(storage);
    if(status==CETTA_TENSOR_OK) finish(*out);
done:
    free(scale); free(squares); return status;
}
CettaTensorStatus cetta_tensor_sum(const CettaTensor *t,CettaTensorSchedule schedule,CettaTensorScalar *out) {
    if(!t || !out || (unsigned)schedule>CETTA_TENSOR_FIXED4) return CETTA_TENSOR_VALUE;
    memset(out,0,sizeof(*out)); size_t n=cetta_tensor_stored(t);
    if(t->dtype==CETTA_TENSOR_F32 || t->dtype==CETTA_TENSOR_F64) {
        double lanes[4]={0,0,0,0};
        for(size_t i=0;i<n;i++) {
            const void *value=stored_value(t,i);
            lanes[schedule==CETTA_TENSOR_FIXED4 ? i%4 : 0]+=read_float(t->dtype,value);
        }
        out->f64=schedule==CETTA_TENSOR_FIXED4 ? ((lanes[0]+lanes[1])+lanes[2])+lanes[3] : lanes[0];
        return CETTA_TENSOR_OK;
    }
    if(t->dtype==CETTA_TENSOR_BOOL) return CETTA_TENSOR_DTYPE;
    for(size_t i=0;i<n;i++) {
        const void *value=stored_value(t,i);
        CettaTensorStatus s=scalar_add(t->dtype,out,value); if(s!=CETTA_TENSOR_OK) return s;
    }
    return CETTA_TENSOR_OK;
}
static inline double packed_f64(const unsigned char *p) { double x; memcpy(&x,p,8); return x; }
static inline double packed_f32(const unsigned char *p) { float x; memcpy(&x,p,4); return x; }
#define PACKED_DOT(name,read_m,mwidth,read_q,qwidth) \
static double name(const unsigned char *m,const unsigned char *q,size_t n,CettaTensorSchedule schedule) { \
    double s0=0,s1=0,s2=0,s3=0; size_t j=0; \
    if(schedule==CETTA_TENSOR_SEQUENTIAL) { \
        for(;j<n;j++) { double p=read_m(m+j*(mwidth))*read_q(q+j*(qwidth)); s0+=p; } \
        return s0; \
    } \
    for(;n-j>=4;j+=4) { \
        s0+=read_m(m+j*(mwidth))*read_q(q+j*(qwidth)); \
        s1+=read_m(m+(j+1)*(mwidth))*read_q(q+(j+1)*(qwidth)); \
        s2+=read_m(m+(j+2)*(mwidth))*read_q(q+(j+2)*(qwidth)); \
        s3+=read_m(m+(j+3)*(mwidth))*read_q(q+(j+3)*(qwidth)); \
    } \
    if(j<n) { s0+=read_m(m+j*(mwidth))*read_q(q+j*(qwidth)); j++; } \
    if(j<n) { s1+=read_m(m+j*(mwidth))*read_q(q+j*(qwidth)); j++; } \
    if(j<n) { s2+=read_m(m+j*(mwidth))*read_q(q+j*(qwidth)); } \
    return ((s0+s1)+s2)+s3; \
}
PACKED_DOT(dot_f64_f64,packed_f64,8,packed_f64,8)
PACKED_DOT(dot_f64_f32,packed_f64,8,packed_f32,4)
PACKED_DOT(dot_f32_f64,packed_f32,4,packed_f64,8)
PACKED_DOT(dot_f32_f32,packed_f32,4,packed_f32,4)
#undef PACKED_DOT

/* Independent rows may share query loads without reassociating any row.
 * Each accumulator still visits columns left-to-right; interleaving rows
 * exposes instruction-level parallelism while preserving the default sum. */
#define PACKED_ROWS(name,read_m,mwidth,read_q,qwidth,dot) \
static void name(const unsigned char *m,const unsigned char *q,size_t rows,size_t cols,double *answer) { \
    size_t row=0,step=cols*(mwidth); \
    for(;rows-row>=4;row+=4) { \
        const unsigned char *a=m+row*step,*b=a+step,*c=b+step,*d=c+step; \
        double s0=0,s1=0,s2=0,s3=0; \
        for(size_t col=0;col<cols;col++) { \
            double x=read_q(q+col*(qwidth)); \
            s0+=read_m(a+col*(mwidth))*x; s1+=read_m(b+col*(mwidth))*x; \
            s2+=read_m(c+col*(mwidth))*x; s3+=read_m(d+col*(mwidth))*x; \
        } \
        answer[row]=s0; answer[row+1]=s1; answer[row+2]=s2; answer[row+3]=s3; \
    } \
    for(;row<rows;row++) answer[row]=dot(m+row*step,q,cols,CETTA_TENSOR_SEQUENTIAL); \
}
PACKED_ROWS(rows_f64_f64,packed_f64,8,packed_f64,8,dot_f64_f64)
PACKED_ROWS(rows_f64_f32,packed_f64,8,packed_f32,4,dot_f64_f32)
PACKED_ROWS(rows_f32_f64,packed_f32,4,packed_f64,8,dot_f32_f64)
PACKED_ROWS(rows_f32_f32,packed_f32,4,packed_f32,4,dot_f32_f32)
#undef PACKED_ROWS

/* Independent selected rows retain their original column schedule. */
#define PACKED_SELECTED(name,read_m,mwidth,read_q,qwidth,dot) \
static void name(const unsigned char *m,const unsigned char *q,const uint64_t *ids, \
    size_t rows,size_t cols,double *answer) { \
    size_t row=0,step=cols*(mwidth); \
    for(;rows-row>=4;row+=4) { \
        const unsigned char *a=m+ids[row]*step,*b=m+ids[row+1]*step; \
        const unsigned char *c=m+ids[row+2]*step,*d=m+ids[row+3]*step; \
        double s0=0,s1=0,s2=0,s3=0; \
        for(size_t col=0;col<cols;col++) { \
            double x=read_q(q+col*(qwidth)); \
            s0+=read_m(a+col*(mwidth))*x; s1+=read_m(b+col*(mwidth))*x; \
            s2+=read_m(c+col*(mwidth))*x; s3+=read_m(d+col*(mwidth))*x; \
        } \
        answer[row]=s0; answer[row+1]=s1; answer[row+2]=s2; answer[row+3]=s3; \
    } \
    for(;row<rows;row++) answer[row]=dot(m+ids[row]*step,q,cols,CETTA_TENSOR_SEQUENTIAL); \
}
PACKED_SELECTED(selected_f64_f64,packed_f64,8,packed_f64,8,dot_f64_f64)
PACKED_SELECTED(selected_f64_f32,packed_f64,8,packed_f32,4,dot_f64_f32)
PACKED_SELECTED(selected_f32_f64,packed_f32,4,packed_f64,8,dot_f32_f64)
PACKED_SELECTED(selected_f32_f32,packed_f32,4,packed_f32,4,dot_f32_f32)
#undef PACKED_SELECTED

CettaTensorStatus cetta_tensor_matvec_rows(const CettaTensor *matrix,const CettaTensor *query,
    const CettaTensor *rows,CettaTensor **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL;
    if(!matrix || !query || !rows) return CETTA_TENSOR_VALUE;
    if((matrix->dtype!=CETTA_TENSOR_F32 && matrix->dtype!=CETTA_TENSOR_F64) ||
       (query->dtype!=CETTA_TENSOR_F32 && query->dtype!=CETTA_TENSOR_F64) || rows->dtype!=CETTA_TENSOR_INDEX)
        return CETTA_TENSOR_DTYPE;
    if(matrix->rank!=2 || query->rank!=1 || query->shape[0]!=matrix->shape[1] ||
        rows->rank!=1 || cetta_tensor_stored(rows)!=rows->count ||
        cetta_tensor_stored(query)!=query->count) return CETTA_TENSOR_SHAPE;
    size_t count=rows->count;
    if(!count) return cetta_tensor_new(CETTA_TENSOR_F64,&count,1,NULL,0,out);
    if(count>SIZE_MAX/sizeof(uint64_t) || count>SIZE_MAX/sizeof(double)) return CETTA_TENSOR_OVERFLOW;
    uint64_t *ids=malloc(count*sizeof(*ids));
    if(!ids) return CETTA_TENSOR_NOMEM;
    for(size_t i=0;i<count;i++) {
        const void *at=element(rows,i);
        if(!at) { free(ids); return CETTA_TENSOR_SHAPE; }
        memcpy(&ids[i],at,sizeof(*ids));
        if(ids[i]>=matrix->shape[0]) { free(ids); return CETTA_TENSOR_INDEX_ERROR; }
    }
    if(matrix->storage->sparse && cetta_tensor_all_finite(matrix) && cetta_tensor_all_finite(query)) {
        double *values=calloc(count,sizeof(*values));
        if(!values) { free(ids); return CETTA_TENSOR_NOMEM; }
        size_t columns=matrix->shape[1];
        for(size_t row=0;row<count;row++) {
            size_t first=(size_t)ids[row]*columns,last=first+columns;
            size_t lo=0,hi=matrix->storage->stored;
            while(lo<hi) {
                size_t mid=lo+(hi-lo)/2;
                if(stored_coordinate(matrix,mid)<first) lo=mid+1; else hi=mid;
            }
            for(size_t i=lo;i<matrix->storage->stored && stored_coordinate(matrix,i)<last;i++) {
                size_t column=stored_coordinate(matrix,i)-first;
                double product=read_float(matrix->dtype,stored_value(matrix,i))*
                    read_float(query->dtype,element(query,column));
                values[row]+=product;
            }
        }
        free(ids);
        CettaTensorStatus status=cetta_tensor_new_take(CETTA_TENSOR_F64,&count,1,
            values,count*sizeof(*values),out);
        if(status!=CETTA_TENSOR_OK) free(values);
        return status;
    }
    if(matrix->storage->sparse || !matrix->contiguous || query->storage->sparse ||
        !query->contiguous || !cetta_tensor_all_finite(matrix) || !cetta_tensor_all_finite(query)) {
        CettaTensor *all=NULL;
        CettaTensorStatus status=cetta_tensor_matvec(matrix,query,CETTA_TENSOR_SEQUENTIAL,&all);
        if(status==CETTA_TENSOR_OK) status=cetta_tensor_gather(all,rows,out);
        cetta_tensor_release(all); free(ids); return status;
    }
    double *values=calloc(count,sizeof(*values));
    if(!values) { free(ids); return CETTA_TENSOR_NOMEM; }
    size_t columns=matrix->shape[1];
    if(columns) {
        size_t mw=cetta_tensor_itemsize(matrix->dtype),qw=cetta_tensor_itemsize(query->dtype);
        void (*multiply)(const unsigned char *,const unsigned char *,const uint64_t *,size_t,size_t,double *)=
            matrix->dtype==CETTA_TENSOR_F64 ?
                (query->dtype==CETTA_TENSOR_F64 ? selected_f64_f64 : selected_f64_f32) :
                (query->dtype==CETTA_TENSOR_F64 ? selected_f32_f64 : selected_f32_f32);
        multiply(matrix->storage->values+matrix->offset*mw,query->storage->values+query->offset*qw,
            ids,count,columns,values);
    }
    free(ids);
    CettaTensorStatus status=cetta_tensor_new_take(CETTA_TENSOR_F64,&count,1,values,count*sizeof(*values),out);
    if(status!=CETTA_TENSOR_OK) free(values);
    return status;
}

/* Compressed indexes remove coordinate division and generic lookup from the
 * numerical loop. Independent row accumulators retain their column order;
 * canonical observations use stored_coordinate/stored_value, not a COO copy. */
#define PACKED_SPARSE(name,read_m,mwidth,read_q,qwidth) \
static CettaTensorStatus name(const CettaTensor *m,const unsigned char *q,CettaTensorSchedule schedule,double *answer) { \
    const unsigned char *values=m->storage->values; \
    size_t rows=m->shape[0],cols=m->shape[1],cursor=0; \
    if(m->layout==CETTA_TENSOR_CSC) { \
        if(schedule==CETTA_TENSOR_FIXED4) { \
            if(rows>SIZE_MAX/(4*sizeof(double))) return CETTA_TENSOR_OVERFLOW; \
            double *lanes=rows ? calloc(rows,4*sizeof(double)) : NULL; \
            uint8_t *occurrences=rows ? calloc(rows,sizeof(uint8_t)) : NULL; \
            if(rows && (!lanes || !occurrences)) { free(lanes); free(occurrences); return CETTA_TENSOR_NOMEM; } \
            for(size_t col=0;col<cols;col++) { \
                double query=read_q(q+col*(qwidth)); \
                for(size_t i=m->segments[col];i<m->segments[col+1];i++) { \
                    size_t row=m->minor[i],lane=occurrences[row]; \
                    occurrences[row]=(uint8_t)((lane+1u)%4u); \
                    lanes[lane*rows+row]+=read_m(values+i*(mwidth))*query; \
                } \
            } \
            for(size_t row=0;row<rows;row++) { \
                answer[row]=((lanes[row]+lanes[rows+row])+lanes[2*rows+row])+lanes[3*rows+row]; \
            } \
            free(lanes); free(occurrences); return CETTA_TENSOR_OK; \
        } \
        for(size_t col=0;col<cols;col++) { \
            double query=read_q(q+col*(qwidth)); \
            for(size_t i=m->segments[col];i<m->segments[col+1];i++) \
                answer[m->minor[i]]+=read_m(values+i*(mwidth))*query; \
        } \
        return CETTA_TENSOR_OK; \
    } \
    for(size_t row=0;row<rows;row++) { \
        size_t base=row*cols,end; \
        if(m->layout==CETTA_TENSOR_CSR) { cursor=m->segments[row]; end=m->segments[row+1]; } \
        else { end=cursor; while(end<m->storage->stored && m->storage->coordinates[end]<base+cols) end++; } \
        double lanes[4]={0,0,0,0}; size_t occurrence=0; \
        if(schedule==CETTA_TENSOR_SEQUENTIAL) { \
            double total=0; \
            for(;cursor<end;cursor++) { \
                size_t col=m->layout==CETTA_TENSOR_CSR ? m->minor[cursor] : m->storage->coordinates[cursor]-base; \
                total+=read_m(values+cursor*(mwidth))*read_q(q+col*(qwidth)); \
            } \
            answer[row]=total; \
        } else { \
            for(;cursor<end;cursor++,occurrence++) { \
                size_t col=m->layout==CETTA_TENSOR_CSR ? m->minor[cursor] : m->storage->coordinates[cursor]-base; \
                lanes[occurrence%4]+=read_m(values+cursor*(mwidth))*read_q(q+col*(qwidth)); \
            } \
            answer[row]=((lanes[0]+lanes[1])+lanes[2])+lanes[3]; \
        } \
    } \
    return CETTA_TENSOR_OK; \
}
PACKED_SPARSE(sparse_f64_f64,packed_f64,8,packed_f64,8)
PACKED_SPARSE(sparse_f64_f32,packed_f64,8,packed_f32,4)
PACKED_SPARSE(sparse_f32_f64,packed_f32,4,packed_f64,8)
PACKED_SPARSE(sparse_f32_f32,packed_f32,4,packed_f32,4)
#undef PACKED_SPARSE

CettaTensorStatus cetta_tensor_matvec(const CettaTensor *m,const CettaTensor *v,CettaTensorSchedule schedule,CettaTensor **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL;
    if(!m || !v || (unsigned)schedule>CETTA_TENSOR_FIXED4) return CETTA_TENSOR_VALUE;
    if((m->dtype!=CETTA_TENSOR_F32 && m->dtype!=CETTA_TENSOR_F64) ||
       (v->dtype!=CETTA_TENSOR_F32 && v->dtype!=CETTA_TENSOR_F64)) return CETTA_TENSOR_DTYPE;
    if(m->rank!=2 || v->rank!=1 || m->shape[1]!=v->shape[0] || cetta_tensor_stored(v)!=v->count) return CETTA_TENSOR_SHAPE;
    size_t rows=m->shape[0],cols=m->shape[1];
    if(!rows) return cetta_tensor_new(CETTA_TENSOR_F64,&rows,1,NULL,0,out);
    if(rows>SIZE_MAX/sizeof(double)) return CETTA_TENSOR_OVERFLOW;
    double *answer=rows ? calloc(rows,sizeof(double)) : NULL;
    if(rows && !answer) return CETTA_TENSOR_NOMEM;
    if(cols && m->storage->sparse &&
        !v->storage->sparse && v->contiguous &&
        cetta_tensor_all_finite(m) && cetta_tensor_all_finite(v)) {
        size_t vw=cetta_tensor_itemsize(v->dtype);
        const unsigned char *query=v->storage->values+v->offset*vw;
        CettaTensorStatus (*multiply)(const CettaTensor *,const unsigned char *,CettaTensorSchedule,double *)=
            m->dtype==CETTA_TENSOR_F64 ? (v->dtype==CETTA_TENSOR_F64 ? sparse_f64_f64 : sparse_f64_f32) :
                (v->dtype==CETTA_TENSOR_F64 ? sparse_f32_f64 : sparse_f32_f32);
        CettaTensorStatus s=multiply(m,query,schedule,answer);
        if(s==CETTA_TENSOR_OK)
            s=cetta_tensor_new_take(CETTA_TENSOR_F64,&rows,1,answer,rows*sizeof(double),out);
        if(s!=CETTA_TENSOR_OK) free(answer);
        return s;
    }
    if(m->layout==CETTA_TENSOR_CSC && schedule==CETTA_TENSOR_SEQUENTIAL) {
        /* Columns are ascending, so each independent row sees its original
         * column schedule even though the rows are interleaved. */
        for(size_t column=0;column<cols;column++) {
            double q=read_float(v->dtype,element(v,column));
            for(size_t j=m->segments[column];j<m->segments[column+1];j++) {
                size_t row=m->minor[j];
                double product=read_float(m->dtype,m->storage->values+j*cetta_tensor_itemsize(m->dtype))*q;
                answer[row]+=product;
            }
        }
        CettaTensorStatus s=cetta_tensor_new_take(CETTA_TENSOR_F64,&rows,1,answer,rows*sizeof(double),out);
        if(s!=CETTA_TENSOR_OK) free(answer);
        return s;
    }
    if(cols && !m->storage->sparse && !v->storage->sparse && m->contiguous && v->contiguous &&
        cetta_tensor_all_finite(m) && cetta_tensor_all_finite(v)) {
        size_t mw=cetta_tensor_itemsize(m->dtype),vw=cetta_tensor_itemsize(v->dtype);
        const unsigned char *query=v->storage->values+v->offset*vw;
        double (*dot)(const unsigned char *,const unsigned char *,size_t,CettaTensorSchedule)=
            m->dtype==CETTA_TENSOR_F64 ? (v->dtype==CETTA_TENSOR_F64 ? dot_f64_f64 : dot_f64_f32) :
                (v->dtype==CETTA_TENSOR_F64 ? dot_f32_f64 : dot_f32_f32);
        if(schedule==CETTA_TENSOR_SEQUENTIAL) {
            void (*multiply)(const unsigned char *,const unsigned char *,size_t,size_t,double *)=
                m->dtype==CETTA_TENSOR_F64 ? (v->dtype==CETTA_TENSOR_F64 ? rows_f64_f64 : rows_f64_f32) :
                    (v->dtype==CETTA_TENSOR_F64 ? rows_f32_f64 : rows_f32_f32);
            multiply(m->storage->values+m->offset*mw,query,rows,cols,answer);
        } else for(size_t row=0;row<rows;row++) answer[row]=dot(m->storage->values+(m->offset+row*cols)*mw,query,cols,schedule);
        CettaTensorStatus s=cetta_tensor_new_take(CETTA_TENSOR_F64,&rows,1,answer,rows*sizeof(double),out);
        if(s!=CETTA_TENSOR_OK) free(answer);
        return s;
    }
    size_t cursor=0;
    for(size_t row=0;row<rows;row++) {
        double lanes[4]={0,0,0,0}; size_t j=0;
        if(m->layout==CETTA_TENSOR_CSR) cursor=m->segments[row];
        if(m->storage->sparse) {
            while(cursor<m->storage->stored && stored_coordinate(m,cursor)/cols==row) {
                size_t flat=stored_coordinate(m,cursor),col=flat%cols;
                double product=read_float(m->dtype,stored_value(m,cursor))*read_float(v->dtype,element(v,col));
                lanes[schedule==CETTA_TENSOR_FIXED4 ? j%4 : 0]+=product; j++; cursor++;
            }
        } else for(size_t col=0;col<cols;col++) {
            double product=read_float(m->dtype,element(m,row*cols+col))*read_float(v->dtype,element(v,col));
            lanes[schedule==CETTA_TENSOR_FIXED4 ? col%4 : 0]+=product;
        }
        answer[row]=schedule==CETTA_TENSOR_FIXED4 ? ((lanes[0]+lanes[1])+lanes[2])+lanes[3] : lanes[0];
    }
    CettaTensorStatus s=cetta_tensor_new_take(CETTA_TENSOR_F64,&rows,1,answer,rows*sizeof(double),out);
    if(s!=CETTA_TENSOR_OK) free(answer);
    return s;
}
typedef struct { double score; uint64_t row; } RankedRow;
static int rank_compare(const void *a,const void *b) {
    const RankedRow *x=a,*y=b;
    if(x->score!=y->score) return x->score>y->score ? -1 : 1;
    return x->row<y->row ? -1 : x->row!=y->row;
}
CettaTensorStatus cetta_tensor_top_k(const CettaTensor *scores,const CettaTensor *mask,size_t k,CettaTensor **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL;
    if(!scores) return CETTA_TENSOR_VALUE;
    if(scores->dtype!=CETTA_TENSOR_F32 && scores->dtype!=CETTA_TENSOR_F64) return CETTA_TENSOR_DTYPE;
    if(scores->rank!=1 || (mask && (mask->dtype!=CETTA_TENSOR_BOOL || mask->rank!=1 || mask->count!=scores->count))) return CETTA_TENSOR_SHAPE;
    size_t n=cetta_tensor_stored(scores);
    if(k>n) k=n;
    if(k>SIZE_MAX/sizeof(RankedRow)) return CETTA_TENSOR_OVERFLOW;
    RankedRow *ranked=k ? malloc(k*sizeof(*ranked)) : NULL;
    if(k && !ranked) return CETTA_TENSOR_NOMEM;
    size_t selected=0;
    for(size_t i=0;i<n;i++) {
        size_t coordinate=stored_coordinate(scores,i); const uint8_t *enabled=mask ? element(mask,coordinate) : NULL;
        if(mask && (!enabled || !*enabled)) continue;
        const void *value=stored_value(scores,i);
        double score=read_float(scores->dtype,value);
        if(!isfinite(score)) { free(ranked); return CETTA_TENSOR_VALUE; }
        RankedRow candidate={score,coordinate};
        if(selected<k) {
            size_t child=selected++; ranked[child]=candidate;
            while(child) {
                size_t parent=(child-1)/2;
                if(rank_compare(&ranked[child],&ranked[parent])<=0) break;
                RankedRow swap=ranked[parent]; ranked[parent]=ranked[child]; ranked[child]=swap; child=parent;
            }
        } else if(k && rank_compare(&candidate,&ranked[0])<0) {
            ranked[0]=candidate; size_t parent=0;
            for(;;) {
                size_t child=parent*2+1; if(child>=k) break;
                if(child+1<k && rank_compare(&ranked[child+1],&ranked[child])>0) child++;
                if(rank_compare(&ranked[child],&ranked[parent])<=0) break;
                RankedRow swap=ranked[parent]; ranked[parent]=ranked[child]; ranked[child]=swap; parent=child;
            }
        }
    }
    if(selected) qsort(ranked,selected,sizeof(*ranked),rank_compare);
    k=selected;
    uint64_t *indices=k ? malloc(k*sizeof(*indices)) : NULL;
    if(k && !indices) { free(ranked); return CETTA_TENSOR_NOMEM; }
    for(size_t i=0;i<k;i++) indices[i]=ranked[i].row;
    CettaTensorStatus s=cetta_tensor_new(CETTA_TENSOR_INDEX,&k,1,indices,k*sizeof(*indices),out);
    free(ranked); free(indices); return s;
}
static bool class_equal(const void *a,const void *b) { return cetta_tensor_equal(a,b); }
static int class_compare(const void *a,const void *b) { return cetta_tensor_compare(a,b); }
static uint64_t class_hash(const void *a) { return cetta_tensor_hash(a); }
static size_t class_bytes(const void *a) { return cetta_tensor_retained_bytes(a); }
static size_t class_display(const void *raw,char *buffer,size_t capacity) {
    const CettaTensor *t=raw;
    int n=snprintf(buffer,capacity,"(Tensor %s rank:%zu size:%zu stored:%zu)",cetta_tensor_dtype_name(t->dtype),t->rank,t->count,cetta_tensor_stored(t));
    return n<0 ? 0 : (size_t)n;
}
static Atom *scalar_atom(Arena *a,CettaTensorDType d,const void *raw) {
    CettaTensorScalar x={0}; memcpy(&x,raw,cetta_tensor_itemsize(d));
    switch(d) {
    case CETTA_TENSOR_BOOL: return atom_bool(a,x.u8!=0);
    case CETTA_TENSOR_U8: return atom_int(a,x.u8);
    case CETTA_TENSOR_I64: return atom_int(a,x.i64);
    case CETTA_TENSOR_INDEX: {
        if(x.index<=INT64_MAX) return atom_int(a,(int64_t)x.index);
        char text[32]; snprintf(text,sizeof(text),"%" PRIu64,x.index); return atom_bigint(a,text);
    }
    case CETTA_TENSOR_F32: return atom_float(a,x.f32);
    case CETTA_TENSOR_F64: return atom_float(a,x.f64);
    }
    return NULL;
}
static Atom *class_inspect(const void *raw,Arena *a) {
    const CettaTensor *t=raw; size_t n=cetta_tensor_stored(t);
    Atom **shape=t->rank ? arena_alloc(a,t->rank*sizeof(*shape)) : NULL;
    for(size_t i=0;i<t->rank;i++) {
        uint64_t dimension=t->shape[i]; shape[i]=scalar_atom(a,CETTA_TENSOR_INDEX,&dimension);
    }
    Atom **rows=n ? arena_alloc(a,n*sizeof(*rows)) : NULL;
    for(size_t i=0;i<n;i++) {
        uint64_t coordinate=stored_coordinate(t,i);
        rows[i]=atom_expr2(a,scalar_atom(a,CETTA_TENSOR_INDEX,&coordinate),scalar_atom(a,t->dtype,element(t,(size_t)coordinate)));
    }
    Atom *fields[]={atom_symbol(a,"TensorData"),atom_symbol(a,cetta_tensor_dtype_name(t->dtype)),atom_expr(a,shape,t->rank),atom_expr(a,rows,n)};
    return atom_expr(a,fields,4);
}
static const CettaStructuralClass tensor_class={"Tensor",cetta_tensor_retain,cetta_tensor_release,class_equal,class_compare,class_hash,class_display,class_inspect,class_bytes};
Atom *cetta_tensor_atom(Arena *a,CettaTensor *t) { return t ? atom_structural(a,t) : NULL; }
CettaTensor *cetta_tensor_from_atom(const Atom *a) {
    return atom_is_structural(a) && cetta_structural_class(a->ground.ptr)==&tensor_class ? a->ground.ptr : NULL;
}
static void put_le(unsigned char *p,uint64_t word,size_t width) { for(size_t i=0;i<width;i++) p[i]=(unsigned char)(word>>(8*i)); }
static uint64_t get_le(const unsigned char *p,size_t width) { uint64_t word=0; for(size_t i=0;i<width;i++) word|=(uint64_t)p[i]<<(8*i); return word; }
CettaTensorStatus cetta_tensor_copy_values(const CettaTensor *t,void *bytes,size_t size) {
    if(!t || (size && !bytes)) return CETTA_TENSOR_VALUE;
    if(t->storage->sparse) return CETTA_TENSOR_ENCODING;
    size_t width=cetta_tensor_itemsize(t->dtype);
    if(size!=t->count*width) return CETTA_TENSOR_COUNT;
    if(!size) return CETTA_TENSOR_OK;
    if(t->contiguous) memcpy(bytes,t->storage->values+t->offset*width,size);
    else for(size_t i=0;i<t->count;i++) memcpy((unsigned char *)bytes+i*width,element(t,i),width);
    return CETTA_TENSOR_OK;
}
CettaTensorStatus cetta_tensor_encode(const CettaTensor *t,unsigned char **out,size_t *size) {
    if(!out || !size || !t) return CETTA_TENSOR_VALUE;
    *out=NULL; *size=0;
    if(t->storage->sparse || t->dtype>CETTA_TENSOR_F32 || t->rank>UINT32_MAX) return CETTA_TENSOR_ENCODING;
    size_t width=cetta_tensor_itemsize(t->dtype),header,bytes=t->count*width;
    if(__builtin_mul_overflow(t->rank,(size_t)8,&header) || __builtin_add_overflow(header,(size_t)9,&header) || bytes>SIZE_MAX-header) return CETTA_TENSOR_OVERFLOW;
    unsigned char *p=malloc(header+bytes); if(!p) return CETTA_TENSOR_NOMEM;
    memcpy(p,"CTN1",4); p[4]=(unsigned char)t->dtype; put_le(p+5,t->rank,4);
    for(size_t i=0;i<t->rank;i++) put_le(p+9+8*i,t->shape[i],8);
    for(size_t i=0;i<t->count;i++) put_le(p+header+i*width,scalar_bits(element(t,i),width),width);
    *out=p; *size=header+bytes; return CETTA_TENSOR_OK;
}
CettaTensorStatus cetta_tensor_decode(const unsigned char *p,size_t size,CettaTensor **out) {
    if(!out) return CETTA_TENSOR_VALUE;
    *out=NULL;
    if(!p || size<9 || memcmp(p,"CTN1",4) || p[4]<1 || p[4]>CETTA_TENSOR_F32) return CETTA_TENSOR_ENCODING;
    size_t rank=(size_t)get_le(p+5,4),header;
    if(rank>(size-9)/8) return CETTA_TENSOR_ENCODING;
    header=9+rank*8;
    size_t *shape=rank ? malloc(rank*sizeof(*shape)) : NULL;
    if(rank && !shape) return CETTA_TENSOR_NOMEM;
    for(size_t i=0;i<rank;i++) { uint64_t d=get_le(p+9+8*i,8); if(d>SIZE_MAX) { free(shape); return CETTA_TENSOR_OVERFLOW; } shape[i]=(size_t)d; }
    size_t count,bytes,width=cetta_tensor_itemsize((CettaTensorDType)p[4]);
    CettaTensorStatus s=cetta_tensor_byte_count((CettaTensorDType)p[4],shape,rank,&count,&bytes);
    if(s!=CETTA_TENSOR_OK || bytes!=size-header) { free(shape); return CETTA_TENSOR_ENCODING; }
    unsigned char *data=bytes ? malloc(bytes) : NULL;
    if(bytes && !data) { free(shape); return CETTA_TENSOR_NOMEM; }
    for(size_t i=0;i<count;i++) scalar_from_bits(data+i*width,width,get_le(p+header+i*width,width));
    s=cetta_tensor_new_take((CettaTensorDType)p[4],shape,rank,data,bytes,out);
    if(s!=CETTA_TENSOR_OK) free(data);
    free(shape); return s;
}

#include "tensor_sequence.inc"
