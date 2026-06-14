# my_git

A simplified, **from-scratch** implementation of Git's core internals, written in C++20. `my_git` re-implements staging, content-addressable commits (with a hand-written SHA-1), branching, an LCS-based diff engine, three-way merging with conflict detection, local-path remotes (push/fetch/pull), and a real blob/tree/commit object database with zlib compression — without wrapping or calling the real `git` binary.

This project was built incrementally, phase by phase, as a systems-programming exercise covering file I/O, hashing, graph algorithms, dynamic programming, and content-addressable storage.

---

## Table of Contents

- [Features](#features)
- [Build Instructions](#build-instructions)
- [Command Reference](#command-reference)
- [Repository Layout](#repository-layout)
- [Design Decisions & Internals](#design-decisions--internals)
  - [Full-Snapshot Commit Model](#1-full-snapshot-commit-model)
  - [Content-Addressable Hashing (SHA-1)](#2-content-addressable-hashing-sha-1)
  - [Branches as Pointers + Symbolic HEAD](#3-branches-as-pointers--symbolic-head)
  - [Diff Engine (LCS)](#4-diff-engine-lcs)
  - [Three-Way Merge & Conflict Detection](#5-three-way-merge--conflict-detection)
  - [Remotes (Push / Fetch / Pull)](#6-remotes-push--fetch--pull)
  - [Commit Graph Visualization (Multi-Lane ASCII DAG)](#7-commit-graph-visualization-multi-lane-ascii-dag)
  - [Object Database (Blobs, Trees, Compression)](#8-object-database-blobs-trees-compression)
- [Example Walkthrough](#example-walkthrough)
- [Known Limitations](#known-limitations)
- [Possible Future Work](#possible-future-work)

---

## Features

| Category          | What's implemented                                                                                                                                  |
| ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Local VCS**     | `init`, `add`, `commit`, `log`, `status`                                                                                                            |
| **Hashing**       | Hand-written SHA-1 for content-addressable commit IDs                                                                                               |
| **Branching**     | `branch`, `checkout` (including detached HEAD), symbolic `HEAD` + `refs/`                                                                           |
| **Diffing**       | `diff <hash1> <hash2>` using an LCS-based line diff (`-`/`+` output)                                                                                |
| **Merging**       | `merge <branch>` — merge-base detection, three-way merge, auto-merge, conflict markers, two-parent merge commits                                    |
| **Distributed**   | `remote add`, `push`, `fetch`, `pull` between local repositories                                                                                    |
| **Visualization** | `graph` — multi-lane ASCII DAG with merge connectors, branch labels, and `HEAD` marker                                                              |
| **Object DB**     | `hash-object`, `cat-file -p`, `write-tree`, `ls-tree` — real Git-style content-addressed blob/tree objects with zlib compression and de-duplication |

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
    ├── staging/              # snapshot copies of staged files
    ├── commits/
    │   └── <sha1-hash>/
    │       ├── files/        # full snapshot of every tracked file at this commit
    │       └── metadata      # message, timestamp, parent, parent2, tree
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

### 1. Full-Snapshot Commit Model

Each commit stores a **complete snapshot** of every tracked file under `commits/<hash>/files/` — not just a diff from the parent. When committing:

1. Copy forward every file from the **parent commit's** snapshot.
2. Overlay the newly **staged** files (new files are added, changed files overwrite the copied-forward version).
3. Write the resulting full snapshot to the new commit's `files/` directory.
4. (Phase 7) Additionally build a **tree object** from the same snapshot and record its hash in `metadata` as `tree: <hash>` — see [Object Database](#8-object-database-blobs-trees-compression).

This makes `status`, `diff`, and `merge` simple — every commit is a self-contained, complete picture of the project, so any two commits can be compared directly without reconstructing history.

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

Phase 7 adds a genuine Git-style **content-addressable object store** under `.my_git/objects/`, layered _alongside_ the full-snapshot commit model (additive, non-breaking — `log`/`diff`/`merge`/`checkout` are untouched).

**Object format.** Every object is stored as `"<type> <size>\0<content>"`, hashed with SHA-1 — this matches real Git's blob-hashing formula exactly (`sha1("hello") = aaf4c61d...` style). The object is written to `.my_git/objects/<first 2 hex chars>/<remaining 38 hex chars>`, identical to Git's real directory layout.

**Compression.** The header is stored uncompressed (so the original size is recoverable without decompressing), followed by a 4-byte compressed-length prefix and the zlib-`deflate`d body (`compress2`/`uncompress`, `Z_BEST_COMPRESSION`). Verified on a 2.5 KB highly-repetitive test file: compressed to roughly 90 bytes (~97% reduction). For very small files, zlib's own framing overhead can make the stored object _slightly larger_ than the input — the same is true of real Git.

**Blobs — `hash-object` / `cat-file -p`.** `hash-object <file>` computes `sha1("blob <size>\0<content>")`, writes the object (skipping the write if it already exists), and prints the hash. `cat-file -p <hash>` reads the object, decompresses it, strips the `"<type> <size>\0"` header, and prints the original content byte-for-byte.

**Trees — `write-tree` / `ls-tree`.** A tree object represents a directory snapshot as a list of entries. Real Git uses a compact binary tree format; `my_git` uses a deliberately simplified **text format**, one line per entry:

```
100644 blob c5639cd2586a9320f1ba8b060ce7b63f1b322566 file1.txt
100644 blob f75b2d6ca3b48adde2c765fcabe74b3f4ff50be6 file2.txt
```

`100644` is Git's real mode string for a regular file. Entries are **sorted by filename** before hashing, so the same set of (filename, content) pairs always produces the same tree hash — required for commits to be deterministic. `write-tree` builds a tree from the current directory; `ls-tree <hash>` prints a tree object's entries.

**Commits point to trees.** `cmd_commit` builds a blob for every file in the snapshot, assembles a tree object from those blobs, and records the tree's hash in `metadata` as `tree: <hash>` (also folded into the commit's own SHA-1 input). The result is a real `commit → tree → blob` graph.

**De-duplication, verified end-to-end.** Two files with identical content (`file1.txt` and a copy `test_dup.txt`) hash to the _same_ blob and only one object is written to disk. More strikingly, blobs created via plain `hash-object` calls are **reused by `commit`** later — `ls-tree` on a commit's tree shows the _exact same_ blob hashes that `hash-object` produced earlier for `file1.txt`/`file2.txt`, proving the object store is a single shared, de-duplicated pool used by both plumbing and porcelain commands.

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
# metadata now contains a "tree: <hash>" line
my_git ls-tree <that-tree-hash>
```

---

## Known Limitations

- Tested with ASCII text files; binary file handling is untested.
- No `.gitignore`-style ignore rules — `status` uses a hardcoded skip-list for `my_git`'s own files.
- `graph` is terminal-ASCII only (no colors/interactive zoom), so very wide histories can become visually dense.
- The tree object format is a **simplified text format**, not real Git's compact binary tree encoding.
- The object database (Phase 7) exists _alongside_ the full-snapshot `files/` model — `checkout`/`diff`/`merge`/`status` still operate on `files/`, not on trees/blobs.
- Single-file `main.cpp` — not yet split into modular headers/sources.
- `push`/`pull` operate on local filesystem paths only; no network transport (HTTP/SSH).

## Possible Future Work

- **Full object-database refactor**: make `checkout`, `diff`, `merge`, and `status` operate purely on `tree`/`blob` objects (reconstructing the working tree from the object store) instead of the `commits/<hash>/files/` snapshot folders.
- Binary tree format matching real Git's encoding.
- Modularize into `include/` + `src/` with separate headers for hashing, diffing, merging, object storage, and repository operations.
- Optional colored graph output and pager integration for large histories.
- Automated unit tests (SHA-1 test vectors, LCS correctness, merge scenarios, object round-trips).

---

## Tech Stack

C++20 · `std::filesystem` · `fstream` · `zlib` · `unordered_map`/`map`/`set` · CMake
