# my_git

A simplified, **from-scratch** implementation of Git's core internals, written in C++17/20. `my_git` re-implements staging, content-addressable commits (with a hand-written SHA-1), branching, an LCS-based diff engine, three-way merging with conflict detection, and local-path remotes (push/fetch/pull) — without wrapping or calling the real `git` binary.

This project was built incrementally, phase by phase, as a systems-programming exercise covering file I/O, hashing, graph algorithms, and dynamic programming.

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
- [Example Walkthrough](#example-walkthrough)
- [Known Limitations](#known-limitations)
- [Possible Future Work](#possible-future-work)

---

## Features

| Category          | What's implemented                                                                                               |
| ----------------- | ---------------------------------------------------------------------------------------------------------------- |
| **Local VCS**     | `init`, `add`, `commit`, `log`, `status`                                                                         |
| **Hashing**       | Hand-written SHA-1 for content-addressable commit IDs                                                            |
| **Branching**     | `branch`, `checkout` (including detached HEAD), symbolic `HEAD` + `refs/`                                        |
| **Diffing**       | `diff <hash1> <hash2>` using an LCS-based line diff (`-`/`+` output)                                             |
| **Merging**       | `merge <branch>` — merge-base detection, three-way merge, auto-merge, conflict markers, two-parent merge commits |
| **Distributed**   | `remote add`, `push`, `fetch`, `pull` between local repositories                                                 |
| **Visualization** | `graph` — prints the commit DAG with parent/parent2 pointers                                                     |

---

## Build Instructions

### Requirements

- C++20 compiler (e.g., MinGW `g++` via MSYS2, or any modern GCC/Clang)
- CMake ≥ 3.15

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
my_git graph                      Show the commit DAG (hash + parent pointers)
my_git remote add <name> <path>   Register a local-path remote
my_git push <remote> <branch>     Push a branch to a remote (fast-forward only)
my_git fetch <remote>             Download new commits into remote-tracking refs
my_git pull <remote> <branch>     fetch + merge in one step
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
    │       └── metadata      # message, timestamp, parent, parent2
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

This makes `status`, `diff`, and `merge` simple — every commit is a self-contained, complete picture of the project, so any two commits can be compared directly without reconstructing history.

### 2. Content-Addressable Hashing (SHA-1)

`my_git` includes a **hand-written SHA-1 implementation** (the standard 80-round Merkle–Damgård construction, verified against known test vectors, e.g. `sha1("hello") = aaf4c61d...`).

A commit's ID is `SHA1(message + timestamp + parent + parent2 + all file contents)`. This means:

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

`graph` output shows the resulting DAG:

```
* <hash-A>  parent=-
* <hash-B>  parent=<hash-A>
* <hash-C>  parent=<hash-A>
* <hash-D>  parent=<hash-B>  parent2=<hash-C>
Branches:
  feature -> <hash-C>
  main    -> <hash-D>
```

---

## Known Limitations

- Tested with ASCII text files; binary file handling is untested.
- No `.gitignore`-style ignore rules — `status` uses a hardcoded skip-list for `my_git`'s own files.
- `graph` prints a flat list with parent pointers rather than a true multi-lane ASCII graph (rendering branch/merge lanes visually is a significant undertaking on its own).
- Single-file `main.cpp` — not yet split into modular headers/sources.
- `push`/`pull` operate on local filesystem paths only; no network transport (HTTP/SSH).

## Possible Future Work

- **Object database refactor**: split snapshots into individual content-addressed `blob`/`tree`/`commit` objects (with de-duplication at the file level) plus `hash-object`/`cat-file`/`ls-tree` commands.
- Modularize into `include/` + `src/` with separate headers for hashing, diffing, merging, and repository operations.
- Multi-lane ASCII/graphical DAG visualization.
- Automated unit tests (SHA-1 test vectors, LCS correctness, merge scenarios).

---

## Tech Stack

C++17/20 · `std::filesystem` · `fstream` · `unordered_map`/`map`/`set` · CMake
