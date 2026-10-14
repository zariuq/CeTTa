"""Compressed storage for the exact bytes of hosted proof articles.

Addresses identify uncompressed canonical text. Reading restores that text and
checks its address; neither compression nor addressing grants proof authority.
Articles are immutable. The source and receiving checkers remain responsible
for deciding whether an opened article establishes its requested claim.
"""

from __future__ import annotations

import gzip
import hashlib
from pathlib import Path


class ArticleStoreError(ValueError):
    pass


class CompressedArticleStore:
    def __init__(self, directory: Path):
        self.directory = directory
        directory.mkdir(parents=True, exist_ok=True)

    def path(self, address: str) -> Path:
        if len(address) != 64 or any(c not in "0123456789abcdef" for c in address):
            raise ArticleStoreError("invalid article address")
        return self.directory / (address + ".sexpr.gz")

    def get(self, address: str, default=None):
        path = self.path(address)
        if not path.exists():
            return default
        try:
            with gzip.open(path, "rb") as stream:
                text = stream.read().decode("utf-8")
        except (OSError, EOFError, UnicodeError) as error:
            raise ArticleStoreError("stored article is unreadable") from error
        if hashlib.sha256(text.encode("utf-8")).hexdigest() != address:
            raise ArticleStoreError("stored article does not match its address")
        return text

    def __setitem__(self, address: str, text: str):
        path = self.path(address)
        if hashlib.sha256(text.encode("utf-8")).hexdigest() != address:
            raise ArticleStoreError("article does not match its address")
        if path.exists():
            if self.get(address) != text:
                raise ArticleStoreError("article address collision")
            return
        # Exclusive creation never replaces a previously retained article.
        with path.open("xb") as output:
            with gzip.GzipFile(fileobj=output, mode="wb", compresslevel=1, mtime=0) as packed:
                packed.write(text.encode("utf-8"))
