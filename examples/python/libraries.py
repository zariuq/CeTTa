"""Call CeTTa libraries and lazily consume Python iterators."""
from cetta import Engine


with Engine() as engine:
    strings = engine.library('str')
    assert strings['starts-with']('CeTTa', 'Ce').one().as_bool()
    py = engine.library('py')
    values = engine.pyobject(iter(range(1_000_000)))
    with py.iter.query(values) as answers:
        print(next(answers).to_python())
        print(next(answers).to_python())
