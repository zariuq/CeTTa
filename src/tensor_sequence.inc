/* Ordered packed sequence operations. The storage helpers are private to
 * tensor.c; no scalar Atoms or mutable published aliases cross this boundary. */

typedef struct {
    size_t group, coordinate, ordinal;
} TensorScanEntry;

static int tensor_scan_entry_compare(const void *left, const void *right) {
    const TensorScanEntry *x = left, *y = right;
    if (x->group != y->group) return x->group < y->group ? -1 : 1;
    return x->coordinate < y->coordinate ? -1 : x->coordinate != y->coordinate;
}

CettaTensorStatus cetta_tensor_scan_sum(const CettaTensor *t, size_t axis,
    CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!t) return CETTA_TENSOR_VALUE;
    if (axis >= t->rank) return CETTA_TENSOR_SHAPE;
    if (t->dtype == CETTA_TENSOR_BOOL) return CETTA_TENSOR_DTYPE;
    size_t n = cetta_tensor_stored(t), width = cetta_tensor_itemsize(t->dtype);
    if (n > SIZE_MAX / sizeof(TensorScanEntry) || n > SIZE_MAX / width)
        return CETTA_TENSOR_OVERFLOW;
    TensorScanEntry *entries = n ? malloc(n * sizeof(*entries)) : NULL;
    TensorStorage *storage = storage_new(n, width, t->storage->sparse);
    if ((n && !entries) || !storage) {
        free(entries); storage_release(storage); return CETTA_TENSOR_NOMEM;
    }
    size_t inner = 1;
    if (n) for (size_t i = axis + 1; i < t->rank; i++) inner *= t->shape[i];
    size_t block = n ? inner * t->shape[axis] : 1;
    for (size_t i = 0; i < n; i++) {
        size_t coordinate = stored_coordinate(t, i);
        entries[i] = (TensorScanEntry){(coordinate / block) * inner + coordinate % inner,
            coordinate, i};
        if (storage->sparse) storage->coordinates[i] = coordinate;
    }
    if (n > 1) qsort(entries, n, sizeof(*entries), tensor_scan_entry_compare);
    CettaTensorStatus status = CETTA_TENSOR_OK;
    CettaTensorScalar total;
    memset(&total, 0, sizeof(total));
    for (size_t i = 0; i < n; i++) {
        const void *value = stored_value(t, entries[i].ordinal);
        if (!i || entries[i].group != entries[i-1].group) memcpy(&total, value, width);
        else status = scalar_add(t->dtype, &total, value);
        if (status != CETTA_TENSOR_OK) break;
        memcpy(storage->values + entries[i].ordinal * width, &total, width);
    }
    if (status == CETTA_TENSOR_OK) {
        status = descriptor(storage, t->dtype, t->shape, t->rank, t->count, out);
        if (status == CETTA_TENSOR_OK) finish(*out);
    }
    free(entries); storage_release(storage); return status;
}

CettaTensorStatus cetta_tensor_concatenate(const CettaTensor *left,
    const CettaTensor *right, size_t axis, CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!left || !right) return CETTA_TENSOR_VALUE;
    if (left->dtype != right->dtype) return CETTA_TENSOR_DTYPE;
    if (left->rank != right->rank || axis >= left->rank) return CETTA_TENSOR_SHAPE;
    for (size_t i = 0; i < left->rank; i++)
        if (i != axis && left->shape[i] != right->shape[i]) return CETTA_TENSOR_SHAPE;
    if (left->shape[axis] > SIZE_MAX - right->shape[axis]) return CETTA_TENSOR_OVERFLOW;
    size_t n = cetta_tensor_stored(left), m = cetta_tensor_stored(right);
    size_t width = cetta_tensor_itemsize(left->dtype);
    if (n > SIZE_MAX - m || n + m > SIZE_MAX / sizeof(size_t) || n + m > SIZE_MAX / width)
        return CETTA_TENSOR_OVERFLOW;
    size_t *shape = malloc(left->rank * sizeof(*shape));
    size_t *coordinates = n + m ? malloc((n + m) * sizeof(*coordinates)) : NULL;
    unsigned char *data = n + m ? malloc((n + m) * width) : NULL;
    if (!shape || (n + m && (!coordinates || !data))) {
        free(shape); free(coordinates); free(data); return CETTA_TENSOR_NOMEM;
    }
    memcpy(shape, left->shape, left->rank * sizeof(*shape));
    shape[axis] += right->shape[axis];
    size_t count = 0, inner = 1;
    CettaTensorStatus status = cetta_tensor_coordinate_count(shape, left->rank, &count);
    if (status != CETTA_TENSOR_OK) goto done;
    if (n + m) for (size_t i = axis + 1; i < left->rank; i++) inner *= shape[i];
    const CettaTensor *inputs[] = {left, right};
    size_t used = 0;
    for (size_t j = 0; j < 2; j++) {
        const CettaTensor *input = inputs[j];
        size_t block = input->shape[axis] * inner;
        for (size_t i = 0; i < cetta_tensor_stored(input); i++) {
            size_t old = stored_coordinate(input, i);
            coordinates[used] = (old / block) * (shape[axis] * inner) + old % block +
                (j ? left->shape[axis] * inner : 0);
            memcpy(data + used * width, stored_value(input, i), width); used++;
        }
    }
    status = cetta_tensor_coo(left->dtype, shape, left->rank, coordinates, data,
        used, CETTA_TENSOR_REJECT, out);
done:
    free(shape); free(coordinates); free(data); return status;
}

CettaTensorStatus cetta_tensor_permute(const CettaTensor *t, const size_t *axes,
    size_t rank, CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!t || (rank && !axes)) return CETTA_TENSOR_VALUE;
    if (rank != t->rank) return CETTA_TENSOR_SHAPE;
    size_t *shape = rank ? malloc(rank * sizeof(*shape)) : NULL;
    size_t *positions = rank ? malloc(rank * sizeof(*positions)) : NULL;
    bool *seen = rank ? calloc(rank, sizeof(*seen)) : NULL;
    if (rank && (!shape || !positions || !seen)) {
        free(shape); free(positions); free(seen); return CETTA_TENSOR_NOMEM;
    }
    for (size_t i = 0; i < rank; i++) {
        if (axes[i] >= rank || seen[axes[i]]) {
            free(shape); free(positions); free(seen); return CETTA_TENSOR_SHAPE;
        }
        seen[axes[i]] = true; shape[i] = t->shape[axes[i]];
    }
    size_t n = cetta_tensor_stored(t), width = cetta_tensor_itemsize(t->dtype);
    if (n > SIZE_MAX / sizeof(size_t) || n > SIZE_MAX / width) {
        free(shape); free(positions); free(seen); return CETTA_TENSOR_OVERFLOW;
    }
    size_t *coordinates = n ? malloc(n * sizeof(*coordinates)) : NULL;
    unsigned char *data = n ? malloc(n * width) : NULL;
    if (n && (!coordinates || !data)) {
        free(shape); free(positions); free(seen); free(coordinates); free(data);
        return CETTA_TENSOR_NOMEM;
    }
    for (size_t i = 0; i < n; i++) {
        size_t old = stored_coordinate(t, i), next = 0;
        for (size_t j = rank; j > 0; j--) {
            positions[j-1] = old % t->shape[j-1]; old /= t->shape[j-1];
        }
        for (size_t j = 0; j < rank; j++) next = next * shape[j] + positions[axes[j]];
        coordinates[i] = next;
        memcpy(data + i * width, stored_value(t, i), width);
    }
    CettaTensorStatus status = cetta_tensor_coo(t->dtype, shape, rank, coordinates,
        data, n, CETTA_TENSOR_REJECT, out);
    free(shape); free(positions); free(seen); free(coordinates); free(data); return status;
}
