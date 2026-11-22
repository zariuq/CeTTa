"""An unfinished chart prefix cannot qualify as a completed relation answer."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import gslt_relation_executor_v1 as executor


def main():
    chart = Path(sys.argv[1]).resolve()
    source = Path("langdef/gslt-il/semantics/finite_indexed_command_v1.metta")
    query = ("(gslt-il-program-atom (gslt-il-program-cons left value "
             "(gslt-il-program-cons right value gslt-il-program-nil)) ?row ?value)")
    complete = executor.answers(chart, (source,), query)
    assert len(complete) == 2
    try:
        executor.answers(chart, (source,), query, max_rounds=1)
    except executor.ExecutorFailure as error:
        assert "unfinished: RoundLimitWithResults" in str(error), str(error)
    else:
        raise AssertionError("an unfinished prefix was accepted as complete")
    empty = executor.answers(chart, (source,), "(gslt-il-classify row unknown ?request)")
    assert empty == []
    print("PASS: complete relation bags, completed emptiness and unfinished prefixes remain distinct")


if __name__ == "__main__":
    main()
