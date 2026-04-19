from dataclasses import dataclass, field

@dataclass(frozen=True)
class CommitInfo:
    sha: str
    subject: str
    body: str
    files: list[str]
    insertions: int
    deletions: int
    categories: list[str]

@dataclass(frozen=True)
class FileChange:
    path: str
    category: str
    subscopes: tuple[str, ...]
    insertions: int
    deletions: int
    commit_count: int
    score: float
    flags: tuple[str, ...] = ()

@dataclass(frozen=True)
class DiffSnippet:
    path: str
    category: str
    subscopes: tuple[str, ...]
    score: float
    reason: str
    patch: str
    flags: tuple[str, ...] = ()

@dataclass(frozen=True)
class CategoryContext:
    name: str
    commit_count: int
    insertions: int
    deletions: int
    commits: list[CommitInfo] = field(default_factory=list)
    files: list[FileChange] = field(default_factory=list)
    snippets: list[DiffSnippet] = field(default_factory=list)
    detected_themes: list[str] = field(default_factory=list)

@dataclass(frozen=True)
class ReleaseContext:
    version: str
    previous_tag: str | None
    head_sha: str
    commit_count: int
    categories: dict[str, CategoryContext]
    cross_cutting_notes: list[str] = field(default_factory=list)
