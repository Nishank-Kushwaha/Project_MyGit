# my_git

A simplified, **from-scratch** implementation of Git's core internals, written in C++20. `my_git` re-implements staging, content-addressable commits (with a hand-written SHA-1), branching, an LCS-based diff engine, three-way merging with conflict detection, local-path remotes (push/fetch/pull), and a real blob/tree/commit object database with zlib compression — without wrapping or calling the real `git` binary.

As of Phase 8, the object database is the **sole storage backend**: `checkout`, `diff`, `merge`, `status`, and `commit` all operate purely on `commit → tree → blob` objects, with no per-commit file snapshots anywhere on disk. Phase 9 extends the tree model to support **nested directories** — `add`, `commit`, and `checkout` all handle recursive subdirectory structure natively.

This project was built incrementally, phase by phase, as a systems-programming exercise covering file I/O, hashing, graph algorithms, dynamic programming, and content-addressable storage.

---

## Table of Contents

- [Features](#features)
- [Build Instructions](#build-instructions)
- [Command Reference](#command-reference)
- [Repository Layout](#repository-layout)
- [Design Decisions & Internals](#design-decisions--internals)
  - [Commit Snapshot Model: Full-Snapshot Semantics, Object-Based Storage](#1-commit-snapshot-model-full-snapshot-semantics-object-based-storage)
  - [Content-Addressable Hashing (SHA-1)](#2-content-addressable-hashing-sha-1)
  - [Branches as Pointers + Symbolic HEAD](#3-branches-as-pointers--symbolic-head)
  - [Diff Engine (LCS)](#4-diff-engine-lcs)
  - [Three-Way Merge & Conflict Detection](#5-three-way-merge--conflict-detection)
  - [Remotes (Push / Fetch / Pull)](#6-remotes-push--fetch--pull)
  - [Commit Graph Visualization (Multi-Lane ASCII DAG)](#7-commit-graph-visualization-multi-lane-ascii-dag)
  - [Object Database (Blobs, Trees, Compression)](#8-object-database-blobs-trees-compression)
  - [Object-Based Architecture (Phase 8)](#9-object-based-architecture-phase-8)
  - [Nested Directory Trees (Phase 9)](#10-nested-directory-trees-phase-9)
- [Example Walkthrough](#example-walkthrough)
- [Known Limitations](#known-limitations)
- [Possible Future Work](#possible-future-work)

---

## Features

| Category          | What's implemented                                                                                                                                                                 |
| ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Local VCS**     | `init`, `add`, `commit`, `log`, `status`                                                                                                                                           |
| **Hashing**       | Hand-written SHA-1 for content-addressable commit IDs                                                                                                                              |
| **Branching**     | `branch`, `checkout` (including detached HEAD), symbolic `HEAD` + `refs/`                                                                                                          |
| **Diffing**       | `diff <hash1> <hash2>` using an LCS-based line diff (`-`/`+` output)                                                                                                               |
| **Merging**       | `merge <branch>` — merge-base detection, three-way merge, auto-merge, conflict markers, two-parent merge commits                                                                   |
| **Distributed**   | `remote add`, `push`, `fetch`, `pull` between local repositories                                                                                                                   |
| **Visualization** | `graph` — multi-lane ASCII DAG with merge connectors, branch labels, and `HEAD` marker                                                                                             |
| **Object DB**     | `hash-object`, `cat-file -p`, `write-tree`, `ls-tree` — real Git-style content-addressed blob/tree objects with zlib compression, de-duplication, and **nested directory support** |
| **Integrity**     | `fsck` — validates the full commit/tree/blob graph and refs; `selftest` — quick sanity checks including commit→tree→blob traversal of every commit                                 |

---

## Build Instructions

### Requirements

- C++20 compiler (e.g., MinGW `g++` via MSYS2, or any modern GCC/Clang)
- CMake ≥ 3.15
- **zlib** (used by the Phase 7 object database for compression)

On MSYS2/MinGW, zlib ships with the toolchain, but if it's missing:

```bash
pacman -S mingw-w64-ucrt-x86_64-zlib
```

`CMakeLists.txt` links it via `target_link_libraries(my_git z)`.

### Build (Windows / PowerShell example)

```powershell
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

### Run

```powershell
cd ..
.\build\my_git.exe help
```

(On Linux/macOS, replace `-G "MinGW Makefiles"` with the default generator, and run `./build/my_git`.)

---

## Command Reference

```
my_git init                       Create empty repository
my_git add <file>                 Stage a file for commit
my_git commit "<message>"         Save staged changes permanently
my_git log                        Show commit history (linear)
my_git status                     Show staged/modified/untracked files
my_git branch [<name>]            List branches, or create a new one
my_git checkout <branch|hash>     Switch branch, or detach HEAD at a commit
my_git diff <hash1> <hash2>       Compare two commit snapshots (LCS diff)
my_git merge <branch>             Three-way merge another branch into current
my_git graph                      Show a multi-lane ASCII commit DAG
my_git remote add <name> <path>   Register a local-path remote
my_git push <remote> <branch>     Push a branch to a remote (fast-forward only)
my_git fetch <remote>             Download new commits into remote-tracking refs
my_git pull <remote> <branch>     fetch + merge in one step
my_git hash-object <file>         Compute a blob hash and store it as an object
my_git cat-file -p <hash>         Print the decompressed contents of an object
my_git write-tree                 Build a tree object from the current directory
my_git ls-tree <hash>             List the entries of a tree object
my_git fsck                       Validate the commit/tree/blob graph and refs
```

---

## Repository Layout

```
project/
├── file1.txt
├── file2.txt
└── .my_git/
    ├── HEAD                  # "ref: refs/main"  OR a raw commit hash (detached HEAD)
    ├── index                 # staged file paths, one per line
    ├── config                # remote.<name>.url=<path>
    ├── staging/              # snapshot copies of staged files (mirrors working-directory structure, including subdirs)
    ├── commits/
    │   └── <sha1-hash>/
    │       └── metadata     # message, timestamp, parent, parent2, tree (Phase 8: no files/ — see Object-Based Architecture)
    ├── objects/
    │   └── <aa>/
    │       └── <...38 hex chars>   # zlib-compressed "<type> <size>\0<content>"
    └── refs/
        ├── main              # commit hash (branch tip)
        ├── feature
        └── remotes/
            └── <remote-name>/
                └── main      # remote-tracking ref
```

---

## Design Decisions & Internals

### 1. Commit Snapshot Model: Full-Snapshot Semantics, Object-Based Storage

Conceptually, every commit represents a **complete snapshot** of every tracked file at that point in time — this hasn't changed since Phase 1. What _has_ changed is **where that snapshot lives**:

- **Phases 1–6**: snapshots were stored as real files under `commits/<hash>/files/`.
- **Phase 7**: each commit _additionally_ recorded a `tree: <hash>` pointing into a content-addressable object store, alongside the existing `files/` folder.
- **Phase 8**: `commits/<hash>/files/` was **removed entirely**. Every snapshot is now reconstructed **on demand** from `commit → tree → blob` objects via `reconstruct_commit()`. See [Object-Based Architecture](#9-object-based-architecture-phase-8) for the full migration story.

When committing, `cmd_commit` now:

1. Reconstructs the **parent's** snapshot via `reconstruct_commit(parent)` (instead of reading `files/`).
2. Overlays the newly **staged** files (new files are added, changed files overwrite the inherited version).
3. Writes a **blob** object for every file in the resulting snapshot, assembles a **tree** object from those blobs, and records `tree: <hash>` in `metadata`.

This makes `status`, `diff`, and `merge` simple — every commit is a self-contained, complete picture of the project (reconstructable purely from objects), so any two commits can be compared directly without walking history.

### 2. Content-Addressable Hashing (SHA-1)

`my_git` includes a **hand-written SHA-1 implementation** (the standard 80-round Merkle–Damgård construction, verified against known test vectors, e.g. `sha1("hello") = aaf4c61d...`).

A commit's ID is `SHA1(message + timestamp + parent + parent2 + tree + all file contents)`. This means:

- **Determinism** — identical content + metadata always produces the same hash.
- **Tamper-evidence** — since each commit's hash input includes its parent's hash, modifying any historical commit changes that commit's hash, which cascades forward through every descendant — exactly like a blockchain.

### 3. Branches as Pointers + Symbolic HEAD

- A branch is just a file in `.my_git/refs/<name>` containing a commit hash.
- `HEAD` is normally **symbolic**: `ref: refs/main` — a pointer _to a pointer_.
- `get_head_commit()` resolves `HEAD → refs/<branch> → commit hash`.
- **Detached HEAD**: `checkout <commit-hash>` writes the raw hash directly into `HEAD` (no `ref:` prefix). New commits made here update `HEAD` directly rather than any branch ref — matching real Git's "may not belong to any branch" warning.
- `checkout` restores the working directory to match the target commit's snapshot, removing files that don't exist there and copying in files that do.

### 4. Diff Engine (LCS)

`my_git diff <hash1> <hash2>` compares two commit snapshots file-by-file using the **Longest Common Subsequence** algorithm on lines:

1. Build an `(n+1) × (m+1)` DP table where `dp[i][j]` = length of the LCS of the first `i` lines of the old file and the first `j` lines of the new file.
2. Backtrack from `dp[n][m]` to `dp[0][0]`, classifying each line as unchanged (` `), removed (`-`), or added (`+`).
3. Reverse the result (since backtracking runs newest→oldest) and print in standard `-`/`+` diff format.

Files that exist in only one snapshot are shown as entirely added or removed.

### 5. Three-Way Merge & Conflict Detection

`my_git merge <branch>` performs a real three-way merge:

1. **Merge-base detection**: collect all ancestors of `HEAD` (`ours`) into a set by walking parent pointers; then walk `theirs`' ancestor chain until the first hash also present in `ours`' ancestor set — that's the merge-base.
2. **Per-file three-way comparison** using `base`, `ours`, and `theirs` versions of each file:
   - `ours == theirs` → no change needed
   - `base == ours` (only theirs changed) → take theirs
   - `base == theirs` (only ours changed) → keep ours
   - **both changed differently** → write `<<<<<<< HEAD / ======= / >>>>>>> <branch>` conflict markers and flag a conflict
3. If conflicts occur, the user manually edits the conflicted files, then `add` + `commit`.
4. The resulting commit has **two parents** (`parent` and `parent2`), making the commit history a true **DAG** rather than a simple chain. `find_merge_base` correctly traverses _through_ prior merge commits when locating ancestors.

### 6. Remotes (Push / Fetch / Pull)

A "remote" is simply **another folder containing its own `.my_git/`** — no networking involved, mirroring how real Git supports local-path remotes (`git remote add origin /path/to/repo`).

- **`push`**: walks the ancestor chain of the local branch tip, copying any `commits/<hash>/` folder not already present in the remote (stopping early once an already-present commit is found, since by the content-addressable invariant its ancestors must already exist too). Performs a **fast-forward check** before updating the remote's ref.
- **`fetch`**: same object-copying logic in reverse, storing results under `refs/remotes/<remote>/<branch>` without touching local branches or the working directory.
- **`pull`**: `fetch` followed by `merge refs/remotes/<remote>/<branch>` — reusing the exact same three-way merge machinery from local merges.

### 7. Commit Graph Visualization (Multi-Lane ASCII DAG)

`my_git graph` renders a true terminal DAG view instead of listing only parent pointers:

1. Commits are loaded from `.my_git/commits/*/metadata` and topologically ordered (Kahn's algorithm), with timestamp-priority tie-breaking for tip ordering.
2. Lanes are assigned dynamically so independent branches, merges, and lane fold-backs remain readable.
3. Per-lane live ranges are computed to draw clean vertical continuity (`|`) across rows.
4. Connector rows render merge and transition geometry with `\`, `/`, and `_`.
5. Each commit row includes short hash, message, branch labels, and a `HEAD` marker when applicable.

### 8. Object Database (Blobs, Trees, Compression)

Phase 7 added a genuine Git-style **content-addressable object store** under `.my_git/objects/`. It started out _alongside_ the full-snapshot `files/` model (additive, non-breaking — `log`/`diff`/`merge`/`checkout` were untouched at that point); Phase 8 ([next section](#9-object-based-architecture-phase-8)) then made it the **sole** source of truth and removed `files/` entirely.

**Object format.** Every object is stored as `"<type> <size>\0<content>"`, hashed with SHA-1 — this matches real Git's blob-hashing formula exactly (`sha1("hello") = aaf4c61d...` style). The object is written to `.my_git/objects/<first 2 hex chars>/<remaining 38 hex chars>`, identical to Git's real directory layout.

**Compression.** The header is stored uncompressed (so the original size is recoverable without decompressing), followed by a 4-byte compressed-length prefix and the zlib-`deflate`d body (`compress2`/`uncompress`, `Z_BEST_COMPRESSION`). Verified on a 2.5 KB highly-repetitive test file: compressed to roughly 90 bytes (~97% reduction). For very small files, zlib's own framing overhead can make the stored object _slightly larger_ than the input — the same is true of real Git.

**Blobs — `hash-object` / `cat-file -p`.** `hash-object <file>` computes `sha1("blob <size>\0<content>")`, writes the object (skipping the write if it already exists), and prints the hash. `cat-file -p <hash>` reads the object, decompresses it, strips the `"<type> <size>\0"` header, and prints the original content byte-for-byte.

**Trees — `write-tree` / `ls-tree`.** A tree object represents a directory snapshot as a list of entries. Real Git uses a compact binary tree format; `my_git` uses a deliberately simplified **text format**, one line per entry:

```
100644 blob c5639cd2586a9320f1ba8b060ce7b63f1b322566 file1.txt
040000 tree e78c44963058fca5ddf90aa4d35c596a3b83b994 nested_test
```

`100644` is Git's real mode string for a regular file; `040000` is the mode for a directory (subtree). Entries are **sorted by name** before hashing, so the same set of (name, content) pairs always produces the same tree hash — required for commits to be deterministic. `write-tree` builds a tree from the current directory (recursively); `ls-tree <hash>` prints a tree object's entries. See [Nested Directory Trees](#10-nested-directory-trees-phase-9) for the full recursive implementation.

**Commits point to trees.** `cmd_commit` builds a blob for every file in the snapshot, assembles a tree object from those blobs, and records the tree's hash in `metadata` as `tree: <hash>` (also folded into the commit's own SHA-1 input). The result is a real `commit → tree → blob` graph.

**De-duplication, verified end-to-end.** Two files with identical content (`file1.txt` and a copy `test_dup.txt`) hash to the _same_ blob and only one object is written to disk. More strikingly, blobs created via plain `hash-object` calls are **reused by `commit`** later — `ls-tree` on a commit's tree shows the _exact same_ blob hashes that `hash-object` produced earlier for `file1.txt`/`file2.txt`, proving the object store is a single shared, de-duplicated pool used by both plumbing and porcelain commands.

### 9. Object-Based Architecture (Phase 8)

Phase 8 is a **core architecture refactor**: every operation that used to read `commits/<hash>/files/` was migrated, one at a time, to reconstruct repository state purely from `commit → tree → blob` objects — and `files/` was then deleted from every commit. The repository is now a real content-addressable object database, exactly like Git's internals.

**The foundation — `load_tree_recursive` and `reconstruct_commit`.**

```cpp
std::map<std::string, std::string> load_tree_recursive(const std::string& tree_hash);
std::map<std::string, std::string> reconstruct_commit(const std::string& commit_hash);
```

`load_tree_recursive` reads a tree object and returns a `path → content` map, decompressing each referenced blob. It recurses into any `tree`-type entries — Phase 9 extended `write_tree` and `cmd_commit` to actually produce nested trees, so this recursion is now exercised in real commits (not just forward-compatible code). `reconstruct_commit` reads the `tree:` hash from a commit's `metadata` and delegates to `load_tree_recursive`. This pair of functions is the **single source of truth** for "what did the project look like at commit X" — everything else is built on top of them.

**The migration, step by step (each verified independently before moving on):**

| Step | Operation      | Change                                                                                                                                                          |
| ---- | -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1    | _(foundation)_ | `load_tree_recursive` + `reconstruct_commit` implemented; cross-checked against the old `files/` folders for **all 12** existing commits — 12/12 byte-identical |
| 2    | `status`       | HEAD comparison source switched to `reconstruct_commit(HEAD)`                                                                                                   |
| 3    | `checkout`     | Both the "remove stale files" and "restore target files" passes now iterate `reconstruct_commit()` snapshots                                                    |
| 4    | `diff`         | Both snapshots reconstructed from objects; LCS diffing logic unchanged                                                                                          |
| 5    | `merge`        | `base`/`ours`/`theirs` all reconstructed from objects before the three-way comparison                                                                           |
| 6    | `commit`       | Copy-forward from parent uses `reconstruct_commit(parent)`; **new commits no longer create a `files/` directory at all** — only `metadata` is written           |
| 7    | `fsck`         | New command validating the whole graph (below)                                                                                                                  |
| 8    | cleanup        | `files/` removed from all pre-existing commits, after a final `fsck` safety check                                                                               |

Each step was re-tested against the exact same scenarios used in earlier phases — branch divergence (Phase 3), line-level diffs (Phase 4), clean _and_ conflicted merges (Phase 5) — with **identical output** before and after the swap.

**`my_git fsck`** walks the entire object graph and reports problems:

- Every commit's `tree:` object exists and is well-formed
- Every blob/sub-tree referenced by a tree (recursively) exists in `.my_git/objects/`
- Every commit's `parent`/`parent2` (if set) point to commits that actually exist (no dangling references)
- Every ref — including `refs/remotes/<remote>/<branch>` — points to a valid commit

A corrupted ref (a branch file rewritten to point at a nonexistent hash) is correctly detected and reported, then clears once the ref is restored — confirming `fsck` actively validates rather than rubber-stamping.

**Final state.** After Step 8, **zero** `commits/<hash>/files/` directories exist anywhere in `.my_git/`. `selftest`'s "Commit → Tree → Blob traversal" check confirms every commit in history reconstructs to a non-empty snapshot purely from objects, and `fsck` reports 0 errors. Branch switching, diffing, and merging across the full `main`/`feature`/`conflict-test` history all continue to work exactly as before — but the entire repository can now be reconstructed from `.my_git/objects/` + `.my_git/commits/*/metadata` + `.my_git/refs/` alone.

### 10. Nested Directory Trees (Phase 9)

Phase 9 closes the last structural gap in the tree model: **subdirectories**. Before this phase, every tree was a flat list of blobs (`100644 blob <hash> <name>`). After Phase 9, trees can contain other trees (`040000 tree <hash> <dirname>`), and the entire stack — `add`, `commit`, `checkout`, `status`, `diff`, `merge` — handles paths like `src/utils/helper.cpp` natively.

**`build_tree_from_map(map<path, content>) → hash`** is the core new function. It takes a flat `map` of `/`-separated paths to file contents (exactly what `reconstruct_commit` returns) and recursively builds a nested tree:

```
Input: {
  "file1.txt"            → "Hello Git"
  "nested_test/hello.cpp" → "int main(){}"
}

Output tree:
  100644 blob <hash> file1.txt
  040000 tree <hash> nested_test
               └── 100644 blob <hash> hello.cpp
```

The algorithm: split each path on its **first** `/`. Paths with no `/` are direct blobs. Paths with a `/` are grouped by their first component into a `subdirs` map, each group then processed by a recursive call. This naturally handles arbitrary nesting depth (`a/b/c/file.txt` → three levels of tree objects).

**`cmd_add` change** — the only other change needed: `fs::path(".my_git/staging") / filename` (dropping `.filename()`) so that staging `nested_test/hello.cpp` writes to `staging/nested_test/hello.cpp` instead of the flat `staging/hello.cpp`. `fs::create_directories` ensures the subdirectory exists inside `staging/` before copying.

**`cmd_commit` change** — switched from parallel `vector<string> all_files/all_contents` to `map<string,string> snapshot` (path → content). `reconstruct_commit(parent)` already returns this exact type, so copy-forward is a one-liner (`snapshot = reconstruct_commit(parent)`). `build_tree_from_map(snapshot)` then replaces the old flat tree-construction block entirely.

**`checkout` cleanup** — after removing a file at `nested_test/hello.cpp`, `remove_empty_dirs_upward` walks upward removing any now-empty parent directories. This ensures the `nested_test/` folder itself disappears when checking out a commit that predates it — not just the file inside it.

**Verified end-to-end:**

```
commit dab31cc  →  tree 691ef0f
  ├── 100644 blob feature_file.txt
  ├── 100644 blob file1.txt
  ├── ...
  └── 040000 tree nested_test (e78c449)
        └── 100644 blob hello.cpp

checkout 070f7fe  →  nested_test/ completely gone (file + empty dir)
checkout main    →  nested_test/hello.cpp correctly restored
```

---

## Example Walkthrough

```powershell
my_git init
my_git add file1.txt
my_git commit "first commit"

my_git branch feature
my_git checkout feature
echo "new feature" > feature.txt
my_git add feature.txt
my_git commit "add feature"

my_git checkout main
my_git merge feature
my_git add feature.txt
my_git commit "Merge feature into main"

my_git graph
```

`graph` output shows the resulting DAG as an ASCII lane graph:

```
*  <hash-D> (HEAD) [main]  "Merge feature into main"
|\
| *  <hash-C> [feature]    "add feature"
* |  <hash-B>              "main-side change"
|/
*  <hash-A>                "first commit"
Branches:
  main             -> <hash-D>
  feature          -> <hash-C>
```

### Object database walkthrough

```powershell
my_git hash-object file1.txt
my_git cat-file -p <hash-from-above>

my_git write-tree
my_git ls-tree <tree-hash-from-above>

my_git commit "a commit"
# metadata now contains a "tree: <hash>" line, and there is NO files/ folder for this commit
my_git ls-tree <that-tree-hash>

my_git fsck          # validates the entire commit/tree/blob graph
my_git selftest      # quick sanity checks, including commit -> tree -> blob traversal
```

### Nested directory walkthrough

```powershell
mkdir nested_test
Set-Content -Encoding ascii -Value "int main(){}" nested_test\hello.cpp
my_git add nested_test/hello.cpp
my_git commit "add nested directory"

# inspect the tree structure
my_git ls-tree <commit-tree-hash>
# 100644 blob ...  file1.txt
# 040000 tree ...  nested_test       <- subdirectory as a tree entry

my_git ls-tree <nested_test-tree-hash>
# 100644 blob ...  hello.cpp

# checkout to an older commit -> nested_test/ disappears (file + empty dir removed)
my_git checkout <older-hash>
# checkout back -> nested_test/hello.cpp fully restored
my_git checkout main
```

---

## Known Limitations

- Tested with ASCII text files; binary file handling is untested.
- No `.gitignore`-style ignore rules — `status` uses a hardcoded skip-list for `my_git`'s own files.
- `graph` is terminal-ASCII only (no colors/interactive zoom), so very wide histories can become visually dense.
- The tree object format is a **simplified text format**, not real Git's compact binary tree encoding.
- Nested directories are supported in `add`/`commit`/`checkout`, but `status` and `diff` currently operate on flat path→content maps — deeply nested modification detection is functional but `status`'s directory-iterator loop only scans one level deep for untracked files.
- Single-file `main.cpp` — not yet split into modular headers/sources.
- `push`/`pull` operate on local filesystem paths only; no network transport (HTTP/SSH).

## Possible Future Work

- Binary tree format matching real Git's encoding.
- `status` untracked-file scan extended to walk subdirectories recursively.
- Modularize into `include/` + `src/` with separate headers for hashing, diffing, merging, object storage, and repository operations.
- Optional colored graph output and pager integration for large histories.
- Automated unit tests (SHA-1 test vectors, LCS correctness, merge scenarios, object round-trips).

---

## Tech Stack

C++20 · `std::filesystem` · `fstream` · `zlib` · `unordered_map`/`map`/`set` · CMake
