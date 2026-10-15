"""A Python operation that raises, for tests/petta/python_error_report.metta."""


def refuse():
    raise RuntimeError("backend refused: token=abcdef1234567890, Bearer abcdefghijklmnop")
