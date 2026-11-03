_Static_assert((int)CETTA_LAYOUT_COO == (int)CETTA_TENSOR_COO &&
    (int)CETTA_LAYOUT_CSR == (int)CETTA_TENSOR_CSR && (int)CETTA_LAYOUT_CSC == (int)CETTA_TENSOR_CSC &&
    (int)CETTA_DUPLICATES_REJECT == (int)CETTA_TENSOR_REJECT &&
    (int)CETTA_DUPLICATES_FIRST == (int)CETTA_TENSOR_FIRST &&
    (int)CETTA_DUPLICATES_LAST == (int)CETTA_TENSOR_LAST && (int)CETTA_DUPLICATES_SUM == (int)CETTA_TENSOR_SUM,
    "sparse ABI codes must match their core translation");

cetta_status_t cetta_embed_tensor_describe(const cetta_atom_t *atom, cetta_tensor_info_t *info) {
    if (!atom || !info || info->size != sizeof(*info)) return CETTA_INVALID_ARGUMENT;
    CettaTensor *tensor = cetta_tensor_from_atom(atom->value);
    if (!tensor) return CETTA_BUFFER_INCOMPATIBLE;
    *info = (cetta_tensor_info_t){.size = sizeof(*info),
        .dtype = cetta_tensor_dtype_name(cetta_tensor_dtype(tensor)),
        .layout = (cetta_tensor_layout_t)cetta_tensor_layout(tensor),
        .rank = cetta_tensor_rank(tensor), .shape = cetta_tensor_shape(tensor),
        .stored = cetta_tensor_stored(tensor)};
    return CETTA_OK;
}
static void sparse_parts_release(cetta_sparse_components_t *parts) {
    cetta_embed_buffer_release(parts->values); cetta_embed_buffer_release(parts->coordinates);
    cetta_embed_buffer_release(parts->indptr); cetta_embed_buffer_release(parts->indices);
    *parts = (cetta_sparse_components_t){.size = sizeof(*parts)};
}
static cetta_status_t sparse_part(CettaTensor *tensor, const void *data, size_t count,
        const char *dtype, CettaTensorByteOrder order, unsigned role, cetta_buffer_t **output) {
    size_t width = cetta_tensor_itemsize(buffer_dtype(dtype));
    if (count > SIZE_MAX / width) return CETTA_BUFFER_INCOMPATIBLE;
    cetta_buffer_descriptor_t spec = {.size = sizeof(spec), .dtype = dtype,
        .byte_order = (cetta_byte_order_t)order, .allocation = data,
        .allocation_bytes = count * width, .rank = 1, .shape = &count};
    cetta_buffer_owner_t owner = {tensor, cetta_tensor_retain, cetta_tensor_release};
    cetta_status_t status = cetta_embed_buffer_borrow(&spec, &owner, output);
    if (status == CETTA_OK) { (*output)->sparse_origin = tensor; (*output)->sparse_component = role; }
    return status;
}
cetta_status_t cetta_embed_tensor_components(const cetta_atom_t *atom, cetta_sparse_components_t *output) {
    if (!atom || !output || output->size != sizeof(*output)) return CETTA_INVALID_ARGUMENT;
    *output = (cetta_sparse_components_t){.size = sizeof(*output)};
    CettaTensor *tensor = cetta_tensor_from_atom(atom->value);
    CettaTensorSparseSpan span;
    if (!cetta_tensor_sparse_span(tensor, &span) || sizeof(size_t) != sizeof(uint64_t))
        return CETTA_BUFFER_INCOMPATIBLE;
    cetta_sparse_components_t parts = {.size = sizeof(parts)};
    cetta_status_t status = sparse_part(tensor, span.values, span.entries,
        cetta_tensor_dtype_name(cetta_tensor_dtype(tensor)), span.byte_order, 1u, &parts.values);
    if (status == CETTA_OK && cetta_tensor_layout(tensor) == CETTA_TENSOR_COO)
        status = sparse_part(tensor, span.coordinates, span.entries, "index",
            CETTA_TENSOR_BYTE_ORDER_NATIVE, 2u, &parts.coordinates);
    if (status == CETTA_OK && cetta_tensor_layout(tensor) != CETTA_TENSOR_COO) {
        status = sparse_part(tensor, span.segments, span.segment_count + 1u, "index",
            CETTA_TENSOR_BYTE_ORDER_NATIVE, 3u, &parts.indptr);
        if (status == CETTA_OK) status = sparse_part(tensor, span.minor, span.entries, "index",
            CETTA_TENSOR_BYTE_ORDER_NATIVE, 4u, &parts.indices);
    }
    if (status == CETTA_OK) *output = parts;
    else sparse_parts_release(&parts);
    return status;
}

typedef struct { const cetta_buffer_t *parts[4]; } SparseBufferOwner;
static void sparse_owner_retain(void *pointer) {
    SparseBufferOwner *owner = pointer;
    for (size_t i = 0; i < 4; ++i) cetta_embed_buffer_retain((cetta_buffer_t *)owner->parts[i]);
}
static void sparse_owner_release(void *pointer) {
    SparseBufferOwner *owner = pointer;
    for (size_t i = 0; i < 4; ++i) cetta_embed_buffer_release((cetta_buffer_t *)owner->parts[i]);
    free(owner);
}
static bool sparse_vector(const cetta_buffer_t *buffer, size_t count, bool index) {
    if (!buffer || cetta_tensor_layout(buffer->tensor) != CETTA_TENSOR_DENSE ||
        cetta_tensor_rank(buffer->tensor) != 1 || cetta_tensor_count(buffer->tensor) != count) return false;
    CettaTensorDType dtype = cetta_tensor_dtype(buffer->tensor);
    return !index || dtype == CETTA_TENSOR_INDEX || dtype == CETTA_TENSOR_I64;
}
static cetta_status_t sparse_index_at(const cetta_buffer_t *buffer, size_t position, size_t *output) {
    CettaTensorScalar value; bool present;
    CettaTensorStatus status = cetta_tensor_get(buffer->tensor, position, &present, &value);
    if (status != CETTA_TENSOR_OK || !present) return CETTA_INVALID_ARGUMENT;
    if (cetta_tensor_dtype(buffer->tensor) == CETTA_TENSOR_I64 && value.i64 < 0) return CETTA_INVALID_ARGUMENT;
    uint64_t number = cetta_tensor_dtype(buffer->tensor) == CETTA_TENSOR_I64 ? (uint64_t)value.i64 : value.index;
    if (number > SIZE_MAX) return CETTA_INVALID_ARGUMENT;
    *output = (size_t)number;
    return CETTA_OK;
}
static const void *sparse_packed(const cetta_buffer_t *buffer, bool index) {
    CettaTensorDenseSpan span;
    if (!buffer || !cetta_tensor_dense_span(buffer->tensor, &span) ||
        (cetta_tensor_count(buffer->tensor) > 1u && span.strides[0] != 1) ||
        (index && (sizeof(size_t) != sizeof(uint64_t) || !cetta_tensor_native_order(buffer->tensor)))) return NULL;
    size_t width = cetta_tensor_itemsize(cetta_tensor_dtype(buffer->tensor));
    return span.allocation ? (const unsigned char *)span.allocation + span.offset * width : NULL;
}
static CettaTensor *sparse_original(const cetta_sparse_descriptor_t *spec) {
    CettaTensor *origin = spec->values->sparse_origin;
    if (!origin || spec->values->sparse_component != 1u ||
        (unsigned)cetta_tensor_layout(origin) != (unsigned)spec->layout ||
        cetta_tensor_rank(origin) != spec->rank || (spec->rank &&
        memcmp(cetta_tensor_shape(origin), spec->shape, spec->rank * sizeof(size_t)))) return NULL;
    const cetta_buffer_t *parts[] = {spec->coordinates, spec->indptr, spec->indices};
    for (size_t i = 0; i < 3; ++i)
        if (parts[i] && (parts[i]->sparse_origin != origin || parts[i]->sparse_component != i + 2u)) return NULL;
    return origin;
}

static cetta_status_t sparse_construct(const cetta_sparse_descriptor_t *spec,
        cetta_duplicates_t duplicates, cetta_copy_t copy, CettaTensor **output) {
    *output = NULL;
    if (!spec || spec->size != sizeof(*spec) || (spec->rank && !spec->shape) ||
        spec->layout < CETTA_LAYOUT_COO || spec->layout > CETTA_LAYOUT_CSC || !spec->values ||
        (unsigned)duplicates > CETTA_DUPLICATES_SUM || (unsigned)copy > CETTA_COPY_NEVER) return CETTA_INVALID_ARGUMENT;
    size_t entries = cetta_tensor_count(spec->values->tensor), count;
    if (!sparse_vector(spec->values, entries, false)) return CETTA_BUFFER_INCOMPATIBLE;
    CettaTensorStatus core = cetta_tensor_coordinate_count(spec->shape, spec->rank, &count);
    if (core != CETTA_TENSOR_OK) return tensor_status(core);
    bool coo = spec->layout == CETTA_LAYOUT_COO;
    size_t majors = 0, bound = 0;
    if (coo) {
        if (spec->indptr || spec->indices || !sparse_vector(spec->coordinates, entries, true)) return CETTA_INVALID_ARGUMENT;
    } else {
        if (spec->rank != 2 || spec->coordinates) return CETTA_INVALID_ARGUMENT;
        majors = spec->shape[spec->layout == CETTA_LAYOUT_CSR ? 0 : 1];
        bound = spec->shape[spec->layout == CETTA_LAYOUT_CSR ? 1 : 0];
        if (majors == SIZE_MAX || !sparse_vector(spec->indptr, majors + 1u, true) ||
            !sparse_vector(spec->indices, entries, true)) return CETTA_INVALID_ARGUMENT;
    }
    /* Negative signed indices are invalid even when their physical unsigned
     * representation could otherwise be borrowed. Validate all metadata
     * before selecting a representation or acquiring external owners. */
    const cetta_buffer_t *index_parts[] = {spec->coordinates, spec->indptr, spec->indices};
    for (size_t part = 0; part < 3; ++part) if (index_parts[part])
        for (size_t i = 0; i < cetta_tensor_count(index_parts[part]->tensor); ++i) {
            size_t value;
            if (sparse_index_at(index_parts[part], i, &value) != CETTA_OK) return CETTA_INVALID_ARGUMENT;
        }
    if (copy != CETTA_COPY_ALWAYS) {
        CettaTensor *original = sparse_original(spec);
        if (original) { cetta_tensor_retain(original); *output = original; return CETTA_OK; }
        const void *values = sparse_packed(spec->values, false);
        const size_t *coordinates = sparse_packed(spec->coordinates, true);
        const size_t *segments = sparse_packed(spec->indptr, true);
        const size_t *minor = sparse_packed(spec->indices, true);
        bool packed = (!entries || values) && (coo ? (!entries || coordinates) : segments && (!entries || minor));
        if (packed) {
            SparseBufferOwner *lease = malloc(sizeof(*lease));
            if (!lease) return CETTA_OUT_OF_MEMORY;
            *lease = (SparseBufferOwner){{spec->values, spec->coordinates, spec->indptr, spec->indices}};
            CettaTensorBufferOwner owner = {lease, sparse_owner_retain, sparse_owner_release};
            CettaTensorDenseSpan span;
            cetta_tensor_dense_span(spec->values->tensor, &span);
            core = cetta_tensor_sparse_borrow(cetta_tensor_dtype(spec->values->tensor), span.byte_order,
                (CettaTensorLayout)spec->layout, spec->shape, spec->rank, coordinates, segments, minor,
                values, entries, &owner, output);
            if (core == CETTA_TENSOR_OK) return CETTA_OK;
            free(lease);
            if (core == CETTA_TENSOR_NOMEM || core == CETTA_TENSOR_INDEX_ERROR ||
                core == CETTA_TENSOR_COUNT || core == CETTA_TENSOR_VALUE) return tensor_status(core);
            if (core == CETTA_TENSOR_DUPLICATE && duplicates == CETTA_DUPLICATES_REJECT) return CETTA_INVALID_ARGUMENT;
        }
        if (copy == CETTA_COPY_NEVER) return CETTA_BUFFER_INCOMPATIBLE;
    }
    size_t width = cetta_tensor_itemsize(cetta_tensor_dtype(spec->values->tensor));
    if (entries > SIZE_MAX / sizeof(size_t) || entries > SIZE_MAX / width) return CETTA_INVALID_ARGUMENT;
    size_t *keys = entries ? malloc(entries * sizeof(*keys)) : NULL;
    unsigned char *values = entries ? malloc(entries * width) : NULL;
    if (entries && (!keys || !values)) { free(keys); free(values); return CETTA_OUT_OF_MEMORY; }
    cetta_status_t status = CETTA_OK;
    if (coo) {
        for (size_t i = 0; i < entries; ++i) {
            status = sparse_index_at(spec->coordinates, i, &keys[i]);
            if (status != CETTA_OK) break;
        }
    } else {
        size_t start = 0;
        status = sparse_index_at(spec->indptr, 0, &start);
        if (status == CETTA_OK && start) status = CETTA_INVALID_ARGUMENT;
        for (size_t major = 0; status == CETTA_OK && major < majors; ++major) {
            size_t end = 0;
            status = sparse_index_at(spec->indptr, major + 1u, &end);
            if (status != CETTA_OK || end < start || end > entries) { status = CETTA_INVALID_ARGUMENT; break; }
            for (size_t i = start; i < end; ++i) {
                size_t key;
                status = sparse_index_at(spec->indices, i, &key);
                if (status != CETTA_OK || key >= bound) { status = CETTA_INVALID_ARGUMENT; break; }
                keys[i] = spec->layout == CETTA_LAYOUT_CSR ? major * spec->shape[1] + key : key * spec->shape[1] + major;
            }
            start = end;
        }
        if (start != entries) status = CETTA_INVALID_ARGUMENT;
    }
    CettaTensorDenseSpan span;
    cetta_tensor_dense_span(spec->values->tensor, &span);
    for (size_t i = 0; status == CETTA_OK && i < entries; ++i) {
        ptrdiff_t at = (ptrdiff_t)span.offset + (ptrdiff_t)i * span.strides[0];
        memcpy(values + i * width, (const unsigned char *)span.allocation + (size_t)at * width, width);
    }
    CettaTensor *coo_tensor = NULL;
    if (status == CETTA_OK) status = tensor_status(cetta_tensor_coo_ordered(
        cetta_tensor_dtype(spec->values->tensor), span.byte_order, spec->shape, spec->rank,
        keys, values, entries, (CettaTensorDuplicates)duplicates, &coo_tensor));
    if (status == CETTA_OK) status = tensor_status(cetta_tensor_convert(coo_tensor, (CettaTensorLayout)spec->layout, output));
    cetta_tensor_release(coo_tensor); free(keys); free(values);
    return status;
}
cetta_status_t cetta_embed_tensor_from_sparse(cetta_engine_t *engine, const cetta_sparse_descriptor_t *spec,
        cetta_duplicates_t duplicates, cetta_copy_t copy, cetta_atom_t **output) {
    if (!engine || !output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    CettaTensor *tensor = NULL;
    status = sparse_construct(spec, duplicates, copy, &tensor);
    if (status == CETTA_OK) {
        EmbeddingOwner *owner = owner_new(engine);
        status = owner ? atom_wrap(owner, cetta_tensor_atom(&owner->arena, tensor), output) : CETTA_OUT_OF_MEMORY;
        owner_release(owner);
    }
    cetta_tensor_release(tensor);
    engine_leave(engine, &scope);
    return status;
}
