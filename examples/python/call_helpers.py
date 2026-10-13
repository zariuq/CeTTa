"""Keyword calls for the existing positional Python-object bridge."""


def call_with_keywords(function, positional, pairs):
    return function(*positional, **dict(pairs))
