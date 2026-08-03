class Tensor:
    def __init__(self, values):
        self._values = _copy_nested(values)

    @property
    def shape(self):
        rows = len(self._values)
        columns = len(self._values[0]) if rows else 0
        return (rows, columns)

    def tolist(self):
        return _copy_nested(self._values)


def _copy_nested(values):
    return [
        list(row) if isinstance(row, (list, tuple)) else row
        for row in values
    ]


def tensor(values):
    return Tensor(values)


def matmul(left, right):
    left_rows, left_columns = left.shape
    right_rows, right_columns = right.shape
    if left_columns != right_rows:
        raise ValueError("incompatible matrix dimensions")
    product = [
        [
            sum(
                left._values[row][inner] *
                right._values[inner][column]
                for inner in range(left_columns)
            )
            for column in range(right_columns)
        ]
        for row in range(left_rows)
    ]
    return Tensor(product)
