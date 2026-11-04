"""Admission of the counted-fence lexical service used by extended readers."""

from pathlib import Path

from gslt2parse_schema_v1 import Symbol, parse_presentation


def admit_raw_source_profile(path: Path | None) -> bool:
    if path is None:
        return False
    profile = parse_presentation(path)
    expected = (Symbol("raw-source-region-profile"), 114, 35, 34, 1,
                Symbol("first-closing"), Symbol("utf8-verbatim"))
    if len(profile.rules) != 1 or profile.rules[0].body or profile.rules[0].head != expected:
        raise ValueError("raw source profile must declare the counted-fence UTF-8 service")
    return True
