"""Memory and retrieval.

Five separate stores, kept apart on purpose. The Android personal journal is NOT
one of them: it never leaves the device, so there is no code path here that
could receive it.

1. Project knowledge  - static docs, chunked and retrieved by keyword overlap.
2. Session context    - the current conversation turns.
3. Market snapshot    - the evidence retrieved for this question, with time.
4. Research context   - Quant Lab / ledger summaries.
5. User preferences   - language and verbosity only, no personal data.

Retrieval is deliberately simple: no embedding model is downloaded, because on a
4-core CPU-only box an embedding model competes with the answering model for the
same cores. Keyword overlap over a small, curated corpus is enough for a
documentation lookup and costs nothing.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass, field
from pathlib import Path

_WORD_RE = re.compile(r"[\w\u0600-\u06ff]{2,}")


def _tokens(text: str) -> set[str]:
    return {w.lower() for w in _WORD_RE.findall(text or "")}


@dataclass
class Chunk:
    source: str
    text: str
    score: float = 0.0

    def to_dict(self) -> dict:
        return {"source": self.source, "text": self.text, "score": round(self.score, 3)}


class KnowledgeBase:
    """Keyword-overlap retrieval over curated project documents."""

    def __init__(self, max_chunk_chars: int = 900):
        self.max_chunk_chars = max_chunk_chars
        self._chunks: list[Chunk] = []

    def add_document(self, source: str, text: str) -> None:
        for para in re.split(r"\n\s*\n", text or ""):
            para = para.strip()
            if len(para) < 40:
                continue
            for i in range(0, len(para), self.max_chunk_chars):
                piece = para[i : i + self.max_chunk_chars].strip()
                if len(piece) >= 40:
                    self._chunks.append(Chunk(source=source, text=piece))

    def add_directory(self, root: str | Path, glob: str = "*.md", limit_files: int = 60) -> int:
        root = Path(root)
        if not root.exists():
            return 0
        count = 0
        for path in sorted(root.rglob(glob))[:limit_files]:
            try:
                self.add_document(str(path.relative_to(root)), path.read_text(encoding="utf-8", errors="replace"))
                count += 1
            except OSError:
                continue
        return count

    def retrieve(self, query: str, k: int = 4) -> list[Chunk]:
        q = _tokens(query)
        if not q or not self._chunks:
            return []
        scored: list[Chunk] = []
        for chunk in self._chunks:
            overlap = len(q & _tokens(chunk.text))
            if overlap:
                scored.append(Chunk(chunk.source, chunk.text, overlap / (len(q) ** 0.5)))
        scored.sort(key=lambda c: c.score, reverse=True)
        return scored[:k]

    @property
    def size(self) -> int:
        return len(self._chunks)


@dataclass
class SessionContext:
    """Bounded conversation memory. Oldest turns fall off first."""

    max_turns: int = 8
    turns: list[dict] = field(default_factory=list)

    def add(self, role: str, content: str) -> None:
        self.turns.append({"role": role, "content": content})
        if len(self.turns) > self.max_turns * 2:
            self.turns = self.turns[-(self.max_turns * 2) :]

    def as_messages(self) -> list[dict]:
        return list(self.turns)

    def clear(self) -> None:
        self.turns.clear()


def default_knowledge_base(repo_root: str | Path | None = None) -> KnowledgeBase:
    kb = KnowledgeBase()
    root = Path(repo_root) if repo_root else Path(
        os.environ.get("AYZR_REPO_ROOT", "/workspace/project/asp")
    )
    kb.add_directory(root / "docs" / "ayzr", "*.md", limit_files=40)
    return kb
