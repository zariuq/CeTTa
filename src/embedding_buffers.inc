struct cetta_buffer {
    atomic_uint references;
    CettaTensor *tensor;
    ptrdiff_t *strides;
    CettaTensor *sparse_origin; /* Borrowed through this buffer's tensor owner. */
    unsigned sparse_component;
};
_Static_assert((int)CETTA_BYTE_ORDER_NATIVE == (int)CETTA_TENSOR_BYTE_ORDER_NATIVE &&
    (int)CETTA_BYTE_ORDER_LITTLE == (int)CETTA_TENSOR_BYTE_ORDER_LITTLE &&
    (int)CETTA_BYTE_ORDER_BIG == (int)CETTA_TENSOR_BYTE_ORDER_BIG,
    "buffer ABI byte-order translation must match the tensor core");

static cetta_status_t tensor_status(CettaTensorStatus status) {
    return status == CETTA_TENSOR_OK ? CETTA_OK
        : status == CETTA_TENSOR_NOMEM ? CETTA_OUT_OF_MEMORY
        : status == CETTA_TENSOR_DTYPE || status == CETTA_TENSOR_SHAPE || status == CETTA_TENSOR_NONCANONICAL ? CETTA_BUFFER_INCOMPATIBLE
        : CETTA_INVALID_ARGUMENT;
}
static CettaTensorDType buffer_dtype(const char *name) {
    if (!name) return 0;
#define MATCH_DTYPE(symbol, code, text, width, family, precision) if (!strcmp(name, text)) return CETTA_TENSOR_##symbol;
    CETTA_TENSOR_DTYPE_ROWS(MATCH_DTYPE)
#undef MATCH_DTYPE
    return 0;
}
static cetta_buffer_t *buffer_alloc(size_t rank) {
    if (rank > SIZE_MAX / sizeof(ptrdiff_t)) return NULL;
    cetta_buffer_t *buffer = calloc(1u, sizeof(*buffer));
    if (!buffer) return NULL;
    atomic_init(&buffer->references, 1u);
    buffer->strides = rank ? malloc(rank * sizeof(*buffer->strides)) : NULL;
    if (rank && !buffer->strides) { free(buffer); return NULL; }
    return buffer;
}
void cetta_embed_buffer_retain(cetta_buffer_t *buffer) {
    if (buffer) atomic_fetch_add_explicit(&buffer->references, 1u, memory_order_relaxed);
}
void cetta_embed_buffer_release(cetta_buffer_t *buffer) {
    if (!buffer || atomic_fetch_sub_explicit(&buffer->references, 1u, memory_order_acq_rel) != 1u) return;
    cetta_tensor_release(buffer->tensor);
    free(buffer->strides);
    free(buffer);
}
static cetta_status_t buffer_fill(cetta_buffer_t *buffer, CettaTensor *tensor) {
    CettaTensorDenseSpan span;
    if (!cetta_tensor_dense_span(tensor, &span)) return CETTA_BUFFER_INCOMPATIBLE;
    ptrdiff_t width = (ptrdiff_t)cetta_tensor_itemsize(cetta_tensor_dtype(tensor));
    for (size_t i = 0; i < cetta_tensor_rank(tensor); ++i)
        if (__builtin_mul_overflow(span.strides[i], width, &buffer->strides[i]))
            return CETTA_BUFFER_INCOMPATIBLE;
    buffer->tensor = tensor;
    return CETTA_OK;
}
static void buffer_noop_retain(void *pointer) { (void)pointer; }
static void buffer_free_allocation(void *pointer) { free(pointer); }

static cetta_status_t buffer_construct(const cetta_buffer_descriptor_t *spec,
        const cetta_buffer_owner_t *owner, bool copy, cetta_buffer_t **output) {
    if (!spec || spec->size != sizeof(*spec) || !output ||
        spec->rank > SIZE_MAX / sizeof(ptrdiff_t) || (spec->rank && !spec->shape) ||
        (spec->allocation_bytes && !spec->allocation)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    CettaTensorDType dtype = buffer_dtype(spec->dtype);
    size_t width = cetta_tensor_itemsize(dtype);
    if (!width) return CETTA_BUFFER_INCOMPATIBLE;
    if ((unsigned)spec->byte_order > CETTA_BYTE_ORDER_BIG) return CETTA_INVALID_ARGUMENT;
    if (spec->offset % width) return CETTA_BUFFER_INCOMPATIBLE;
    if (!copy && (!owner || !owner->retain || !owner->release)) return CETTA_INVALID_ARGUMENT;
    if (!copy && spec->allocation_bytes && (uintptr_t)spec->allocation % width)
        return CETTA_BUFFER_INCOMPATIBLE;
    cetta_buffer_t *buffer = buffer_alloc(spec->rank);
    ptrdiff_t *strides = spec->rank ? malloc(spec->rank * sizeof(*strides)) : NULL;
    if (!buffer || (spec->rank && !strides)) {
        cetta_embed_buffer_release(buffer); free(strides); return CETTA_OUT_OF_MEMORY;
    }
    size_t step = 1u;
    cetta_status_t status = CETTA_OK;
    for (size_t i = spec->rank; i > 0u; --i) {
        if (spec->strides) {
            if (spec->strides[i - 1u] % (ptrdiff_t)width) { status = CETTA_BUFFER_INCOMPATIBLE; break; }
            strides[i - 1u] = spec->strides[i - 1u] / (ptrdiff_t)width;
        } else {
            strides[i - 1u] = step <= PTRDIFF_MAX ? (ptrdiff_t)step : 0;
            if (spec->shape[i - 1u] && step <= SIZE_MAX / spec->shape[i - 1u]) step *= spec->shape[i - 1u];
            else step = 0u;
        }
        if (__builtin_mul_overflow(strides[i - 1u], (ptrdiff_t)width, &buffer->strides[i - 1u])) {
            status = CETTA_BUFFER_INCOMPATIBLE; break;
        }
    }
    void *copied = NULL;
    if (status == CETTA_OK && copy && spec->allocation_bytes) {
        copied = malloc(spec->allocation_bytes);
        if (!copied) status = CETTA_OUT_OF_MEMORY;
        else memcpy(copied, spec->allocation, spec->allocation_bytes);
    }
    if (status == CETTA_OK) {
        CettaTensorBufferOwner retained = copy
            ? (CettaTensorBufferOwner){copied, buffer_noop_retain, buffer_free_allocation}
            : (CettaTensorBufferOwner){owner->context, owner->retain, owner->release};
        CettaTensor *tensor = NULL;
        status = tensor_status(cetta_tensor_borrow(dtype, (CettaTensorByteOrder)spec->byte_order,
            copy ? copied : spec->allocation, spec->allocation_bytes, spec->shape, strides,
            spec->rank, spec->offset / width, &retained, &tensor));
        if (status == CETTA_OK) { buffer->tensor = tensor; copied = NULL; }
    }
    free(copied); free(strides);
    if (status == CETTA_OK) *output = buffer;
    else cetta_embed_buffer_release(buffer);
    return status;
}
cetta_status_t cetta_embed_buffer_copy(const cetta_buffer_descriptor_t *spec, cetta_buffer_t **output) {
    return buffer_construct(spec, NULL, true, output);
}
cetta_status_t cetta_embed_buffer_borrow(const cetta_buffer_descriptor_t *spec,
        const cetta_buffer_owner_t *owner, cetta_buffer_t **output) {
    return buffer_construct(spec, owner, false, output);
}
cetta_status_t cetta_embed_buffer_describe(const cetta_buffer_t *buffer, cetta_buffer_descriptor_t *info) {
    if (!buffer || !info || info->size != sizeof(*info)) return CETTA_INVALID_ARGUMENT;
    CettaTensor *tensor = buffer->tensor;
    CettaTensorDenseSpan span;
    if (!cetta_tensor_dense_span(tensor, &span)) return CETTA_BUFFER_INCOMPATIBLE;
    *info = (cetta_buffer_descriptor_t){.size = sizeof(*info),
        .dtype = cetta_tensor_dtype_name(cetta_tensor_dtype(tensor)), .byte_order = (cetta_byte_order_t)span.byte_order,
        .allocation = span.allocation, .allocation_bytes = span.allocation_bytes,
        .offset = span.offset * cetta_tensor_itemsize(cetta_tensor_dtype(tensor)),
        .rank = cetta_tensor_rank(tensor), .shape = cetta_tensor_shape(tensor), .strides = buffer->strides};
    return CETTA_OK;
}
cetta_status_t cetta_embed_buffer_view(const cetta_buffer_t *source, const size_t *shape,
        const ptrdiff_t *strides, size_t rank, size_t offset, cetta_buffer_t **output) {
    if (!source || !output || (rank && (!shape || !strides))) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    cetta_buffer_descriptor_t spec = {.size = sizeof(spec)};
    cetta_status_t status = cetta_embed_buffer_describe(source, &spec);
    if (status != CETTA_OK) return status;
    if (rank == spec.rank && offset == spec.offset && (!rank ||
        (!memcmp(shape, spec.shape, rank * sizeof(*shape)) &&
         !memcmp(strides, spec.strides, rank * sizeof(*strides))))) {
        cetta_embed_buffer_retain((cetta_buffer_t *)source);
        *output = (cetta_buffer_t *)source;
        return CETTA_OK;
    }
    spec.shape = shape; spec.strides = strides; spec.rank = rank; spec.offset = offset;
    cetta_buffer_t *buffer = buffer_alloc(rank);
    ptrdiff_t *elements = rank ? malloc(rank * sizeof(*elements)) : NULL;
    if (!buffer || (rank && !elements)) { cetta_embed_buffer_release(buffer); free(elements); return CETTA_OUT_OF_MEMORY; }
    size_t width = cetta_tensor_itemsize(cetta_tensor_dtype(source->tensor));
    if (offset % width) status = CETTA_BUFFER_INCOMPATIBLE;
    for (size_t i = 0; status == CETTA_OK && i < rank; ++i) {
        if (strides[i] % (ptrdiff_t)width) status = CETTA_BUFFER_INCOMPATIBLE;
        else elements[i] = strides[i] / (ptrdiff_t)width;
    }
    CettaTensor *tensor = NULL;
    if (status == CETTA_OK) status = tensor_status(cetta_tensor_view(source->tensor, shape, elements, rank, offset / width, &tensor));
    if (status == CETTA_OK) {
        status = buffer_fill(buffer, tensor);
        if (status != CETTA_OK) cetta_tensor_release(tensor);
    }
    free(elements);
    if (status == CETTA_OK) *output = buffer;
    else { cetta_embed_buffer_release(buffer); *output = NULL; }
    return status;
}
cetta_status_t cetta_embed_buffer_from_atom(const cetta_atom_t *atom, cetta_buffer_t **output) {
    if (!atom || !output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    CettaTensor *tensor = cetta_tensor_from_atom(atom->value);
    if (!tensor) return CETTA_BUFFER_INCOMPATIBLE;
    CettaNativeMemoryScope memory;
    cetta_native_memory_enter(&memory, atom->owner->engine->memory);
    cetta_buffer_t *buffer = buffer_alloc(cetta_tensor_rank(tensor));
    cetta_native_memory_leave(&memory);
    if (!buffer) return CETTA_OUT_OF_MEMORY;
    cetta_status_t status = buffer_fill(buffer, tensor);
    if (status == CETTA_OK) { cetta_tensor_retain(tensor); *output = buffer; }
    else cetta_embed_buffer_release(buffer);
    return status;
}
static void buffer_owner_retain(void *pointer) { cetta_embed_buffer_retain(pointer); }
static void buffer_owner_release(void *pointer) { cetta_embed_buffer_release(pointer); }
cetta_status_t cetta_embed_buffer_reinterpret(const cetta_buffer_t *source, const char *dtype,
        cetta_byte_order_t byte_order, const size_t *shape, const ptrdiff_t *strides, size_t rank, size_t offset,
        cetta_buffer_t **output) {
    if (!output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    cetta_buffer_descriptor_t spec = {.size = sizeof(spec)};
    cetta_status_t status = cetta_embed_buffer_describe(source, &spec);
    if (status != CETTA_OK) return status;
    if ((!rank || strides) && !strcmp(dtype ? dtype : "", spec.dtype) && byte_order == spec.byte_order)
        return cetta_embed_buffer_view(source, shape, strides, rank, offset, output);
    spec.dtype = dtype; spec.byte_order = byte_order;
    spec.shape = shape; spec.strides = strides; spec.rank = rank; spec.offset = offset;
    cetta_buffer_owner_t owner = {(void *)source, buffer_owner_retain, buffer_owner_release};
    return cetta_embed_buffer_borrow(&spec, &owner, output);
}
cetta_status_t cetta_embed_atom_buffer(cetta_engine_t *engine, const cetta_buffer_t *buffer, cetta_atom_t **output) {
    if (!engine || !buffer || !output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    EmbeddingOwner *owner = owner_new(engine);
    status = owner ? atom_wrap(owner, cetta_tensor_atom(&owner->arena, buffer->tensor), output) : CETTA_OUT_OF_MEMORY;
    owner_release(owner);
    engine_leave(engine, &scope);
    return status;
}
