# my_git

A simplified, **from-scratch** implementation of Git's core internals, written in C++20. `my_git` re-implements staging, content-addressable commits (with a hand-written SHA-1), branching, an LCS-based diff engine, three-way merging with conflict detection, dual-transport remotes — local filesystem paths **and** a real HTTP server (push/fetch/pull/clone over the network) — a `.mygitignore` ignore system, and a real blob/tree/commit object database with zlib compression — without wrapping or calling the real `git` binary.

The object database is the **sole storage backend**: `checkout`, `diff`, `merge`, `status`, and `commit` all operate purely on `commit → tree → blob` objects, with no per-commit file snapshots anywhere on disk. The tree model supports **nested directories** natively — `add`, `commit`, `checkout`, and `status` all handle recursive subdirectory structure. The entire codebase has been modularized into `utils/`, `core/`, and `commands/` layers with separate headers and sources, plus a standalone `my_git_server` executable for the HTTP transport.

This project was built incrementally as a systems-programming exercise covering file I/O, hashing, graph algorithms, dynamic programming, and content-addressable storage.

---

## Table of Contents

- [Features](#features)
- [Project Structure](#project-structure)
- [Build Instructions](#build-instructions)
- [Command Reference](#command-reference)
- [Repository Layout](#repository-layout)
- [Design Decisions & Internals](#design-decisions--internals)
  - [Commit Snapshot Model: Full-Snapshot Semantics, Object-Based Storage](#1-commit-snapshot-model-full-snapshot-semantics-object-based-storage)
  - [Content-Addressable Hashing (SHA-1)](#2-content-addressable-hashing-sha-1)
  - [Branches as Pointers + Symbolic HEAD](#3-branches-as-pointers--symbolic-head)
  - [Diff Engine (LCS)](#4-diff-engine-lcs)
  - [Three-Way Merge & Conflict Detection](#5-three-way-merge--conflict-detection)
  - [Remotes (Push / Fetch / Pull / Clone)](#6-remotes-push--fetch--pull--clone)
  - [Commit Graph Visualization (Multi-Lane ASCII DAG)](#7-commit-graph-visualization-multi-lane-ascii-dag)
  - [Object Database (Blobs, Trees, Compression)](#8-object-database-blobs-trees-compression)
  - [Nested Directory Trees](#9-nested-directory-trees)
  - [.mygitignore Ignore System](#10-mygitignore-ignore-system)
  - [Staging Area Lifecycle](#11-staging-area-lifecycle)
  - [Codebase Modularization](#12-codebase-modularization)
  - [HTTP Server (my_git_server)](#13-http-server-my_git_server)
  - [Read-Only Repository API](#14-read-only-repository-api)
- [Example Walkthrough](#example-walkthrough)
- [Known Limitations](#known-limitations)
- [Possible Future Work](#possible-future-work)

---

## Features

| Category          | What's implemented                                                                                                                                                                |
| ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Local VCS**     | `init`, `add`, `commit`, `log`, `status`                                                                                                                                          |
| **Hashing**       | Hand-written SHA-1 (no OpenSSL) for content-addressable commit and object IDs                                                                                                     |
| **Branching**     | `branch`, `checkout` (including detached HEAD), symbolic `HEAD` + `refs/`                                                                                                         |
| **Diffing**       | `diff <hash1> <hash2>` using an LCS-based line diff (`-`/`+` output)                                                                                                              |
| **Merging**       | `merge <branch>` — merge-base detection, three-way merge, auto-merge, conflict markers, two-parent merge commits                                                                  |
| **Distributed**   | `remote add`, `push`, `fetch`, `pull`, `clone` — full remote workflow over **both** local filesystem paths and HTTP                                                               |
| **Networking**    | Standalone `my_git_server` (cpp-httplib) exposing a repo over HTTP — `GET /refs`, `/refs/<name>`, `/commit/<hash>`, `/object/<hash>`, `POST /push`, `POST /have`                  |
| **Ignore System** | `.mygitignore` — pattern-based ignore rules (directory suffix `/`, glob `*.ext`, exact name) respected by `add` and `status`                                                      |
| **Visualization** | `graph` — multi-lane ASCII DAG with merge connectors, branch labels, and `HEAD` marker                                                                                            |
| **Object DB**     | `hash-object`, `cat-file -p`, `write-tree`, `ls-tree` — real Git-style content-addressed blob/tree objects with zlib compression, deduplication, and **nested directory support** |
| **Integrity**     | `fsck` — validates the full commit/tree/blob graph and refs; `selftest` — built-in unit tests (SHA-1, LCS, object round-trip, blob hash formula, commit→tree→blob traversal)      |
| **Modular Code**  | Fully split into `utils/`, `core/`, `commands/` layers with separate headers and sources; `main.cpp` is only ~180 lines of dispatch                                               |

---

## Project Structure

```
my_git/
├── CMakeLists.txt
├── .mygitignore               ← ignore rules for my_git itself
├── README.md
├── src/
│   ├── main.cpp               ← only main() + command dispatch (~180 lines)
│   ├── server/
│   │   └── main.cpp           ← my_git_server entrypoint: GET routes + POST /push, POST /have
│   ├── utils/
│   │   ├── sha1.cpp           ← hand-written SHA-1 (no OpenSSL)
│   │   ├── file_io.cpp        ← read_file, write_file, read_lines, files_equal, remove_empty_dirs_upward
│   │   ├── time.cpp           ← current_timestamp, parse_timestamp
│   │   └── base64.cpp         ← base64_encode/decode for binary object transfer over JSON
│   ├── core/
│   │   ├── object_store.cpp   ← zlib_compress/decompress, write_object, read_object,
│   │   │                         build_tree_from_map, load_tree_recursive, reconstruct_commit,
│   │   │                         write_tree, object_exists
│   │   ├── refs.cpp           ← get_head_commit, set_head_commit, get_current_branch_ref
│   │   ├── commits.cpp        ← get_ancestors, find_merge_base, get_file_at_commit,
│   │   │                         copy_commits_recursive
│   │   ├── diff_engine.cpp    ← split_lines, build_lcs_table, diff_lines, DiffLine struct
│   │   ├── ignore.cpp         ← load_ignore_rules, matches_ignore_rule
│   │   ├── remote.cpp         ← get_remote_url
│   │   └── http_client.cpp    ← is_http_url, http_get_refs/ref/commit_metadata/object,
│   │                             http_fetch_commits, http_push_branch, http_check_have
│   └── commands/
│       ├── init.cpp
│       ├── add.cpp
│       ├── commit.cpp
│       ├── log.cpp
│       ├── status.cpp
│       ├── diff.cpp
│       ├── branch.cpp
│       ├── checkout.cpp
│       ├── merge.cpp
│       ├── graph.cpp
│       ├── remote.cpp         ← cmd_remote_add, cmd_push, cmd_fetch, cmd_pull, cmd_clone
│       │                         (each branches on is_http_url() for HTTP vs filesystem transport)
│       ├── fsck.cpp           ← cmd_fsck (with static helpers), cmd_selftest
│       ├── plumbing.cpp       ← cmd_hash_object, cmd_cat_file, cmd_write_tree, cmd_ls_tree
│       └── help.cpp
├── include/
│   ├── third_party/
│   │   └── httplib.h          ← vendored single-header HTTP library (yhirose/cpp-httplib)
│   ├── utils/
│   │   ├── sha1.h
│   │   ├── file_io.h
│   │   ├── time.h
│   │   └── base64.h
│   ├── core/
│   │   ├── object_store.h
│   │   ├── refs.h
│   │   ├── commits.h
│   │   ├── diff_engine.h
│   │   ├── ignore.h
│   │   ├── remote.h
│   │   └── http_client.h
│   └── commands/
│       ├── init.h / add.h / commit.h / log.h / status.h
│       ├── diff.h / branch.h / checkout.h / merge.h / graph.h
│       ├── remote.h / fsck.h / plumbing.h / help.h
└── build/
    ├── my_git.exe
    └── my_git_server.exe
```

---

## Build Instructions

### Requirements

- C++20 compiler (e.g., MinGW `g++` via MSYS2, or any modern GCC/Clang)
- CMake ≥ 3.15
- **zlib** (used by the object database for compression)
- **cpp-httplib** — vendored as a single header at `include/third_party/httplib.h` (no separate install needed)
- Threading support — `find_package(Threads REQUIRED)`; on Windows, `ws2_32` and `crypt32` (sockets + TLS plumbing httplib references)

On MSYS2/MinGW, install zlib if missing:

```bash
pacman -S mingw-w64-ucrt-x86_64-zlib
```

`CMakeLists.txt` defines **two executables** from one build: `my_git` (the CLI) and `my_git_server` (the HTTP server). Both link `z` and `Threads::Threads`; `my_git_server` additionally links a minimal slice of `core/` (`object_store.cpp`, `commits.cpp`) so it can validate fast-forwards and write objects directly, without depending on the rest of the CLI's command layer. `include_directories(include)` exposes all headers — including `third_party/httplib.h` — to both targets.

### Build (Windows / PowerShell)

```powershell
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

This produces both `build/my_git.exe` and `build/my_git_server.exe`.

### Run

```powershell
.\build\my_git.exe help
.\build\my_git_server.exe 8080 path\to\repo
```

On Linux/macOS, omit `-G "MinGW Makefiles"` and run `./build/my_git` / `./build/my_git_server`.

---

## Command Reference

```
my_git init                       Create empty repository
my_git add <file>                 Stage a file for commit
my_git commit "<message>"         Save staged changes permanently
my_git log                        Show commit history (linear, newest first)
my_git status                     Show staged/modified/untracked files
my_git branch [<name>]            List branches, or create a new one
my_git checkout <branch|hash>     Switch branch, or detach HEAD at a commit
my_git diff <hash1> <hash2>       Compare two commit snapshots (LCS diff)
my_git merge <branch>             Three-way merge another branch into current
my_git graph                      Show a multi-lane ASCII commit DAG
my_git remote add <name> <path|url>   Register a remote: local filesystem path OR http:// URL
my_git push <remote> <branch>         Push a branch (fast-forward only); HTTP or filesystem transport
my_git fetch <remote>                 Download new commits into remote-tracking refs (HTTP or filesystem)
my_git pull <remote> <branch>         fetch + merge in one step (works identically over HTTP)
my_git clone <source> <dest>          Clone a repository (source: local path OR http:// URL)
my_git hash-object <file>         Compute a blob hash and store it as an object
my_git cat-file -p <hash>         Print the decompressed contents of an object
my_git write-tree                 Build a tree object from the current directory
my_git ls-tree <hash>             List the entries of a tree object
my_git fsck                       Validate the commit/tree/blob graph and refs
my_git selftest                   Run built-in unit tests (5 checks)
my_git help                       Show this command list
```

```
my_git_server <port> <repo_path>  Serve a repo over HTTP (separate executable, not a my_git subcommand)
```

---

## Repository Layout

```
project/
├── file1.txt
├── .mygitignore
└── .my_git/
    ├── HEAD                  # "ref: refs/main"  OR a raw commit hash (detached HEAD)
    ├── index                 # staged file paths, one per line
    ├── config                # remote.<name>.url=<path>  OR  remote.<name>.url=http://host:port
    ├── MERGE_HEAD            # exists only during an in-progress merge (stores second-parent hash)
    ├── staging/              # snapshot copies of staged files (mirrors working-directory structure, including subdirs)
    ├── commits/
    │   └── <sha1-hash>/
    │       └── metadata      # message, timestamp, parent, parent2, tree
    │                         # (no files/ folder — all data is in the object store)
    ├── objects/
    │   └── <aa>/
    │       └── <38 hex chars>  # zlib-compressed blob/tree objects
    └── refs/
        ├── main              # commit hash (branch tip)
        ├── feature
        └── remotes/
            └── <remote-name>/
                └── main      # remote-tracking ref updated by fetch
```

---

## Design Decisions & Internals

### 1. Commit Snapshot Model: Full-Snapshot Semantics, Object-Based Storage

Conceptually, every commit represents a **complete snapshot** of every tracked file at that point in time — this is the same philosophy as real Git (not a delta store). What matters is **where and how** that snapshot is stored:

- **Early phases**: snapshots were stored as real files under `commits/<hash>/files/` — a flat copy of every tracked file per commit. Simple to implement, but expensive: every commit duplicated every unchanged file on disk.
- **Later**: each commit was extended to additionally record a `tree: <hash>` line in its metadata, pointing into a content-addressable object store. The `files/` folder was kept alongside as a compatibility fallback during migration.
- **Current**: `commits/<hash>/files/` has been **removed entirely**. Every snapshot is now reconstructed **on demand** from `commit → tree → blob` objects via `reconstruct_commit()`. Unchanged files cost zero disk space because identical blobs hash to the same object and are stored only once.

When committing, `cmd_commit` now:

1. Reconstructs the **parent's** complete snapshot via `reconstruct_commit(parent)`, which walks the parent's tree object recursively and returns a `map<string, string>` of path → content.
2. Overlays newly **staged** files on top of that map — new files are inserted, changed files overwrite the inherited version, giving the full new snapshot.
3. Writes a **blob** object for every file in the resulting snapshot, assembles a **tree** object (recursively for nested directories) from those blobs via `build_tree_from_map`, and records `tree: <hash>` in the commit's metadata.

This makes `status`, `diff`, and `merge` straightforward — every commit is a self-contained, complete picture of the project, reconstructable purely from the object graph, so any two commits can be compared directly by reconstructing both snapshots and diffing them, without walking history.

### 2. Content-Addressable Hashing (SHA-1)

SHA-1 is implemented from scratch in `src/utils/sha1.cpp` — no OpenSSL, no system library, no dependency beyond the C++ standard library. The implementation follows the FIPS 180-4 specification: message padding to a multiple of 512 bits, 80-round block processing with four stages using `AND`/`OR`/`XOR` bitwise operations and left-rotation, and five 32-bit state words accumulated across all 64-byte chunks.

The hash input follows real Git's content-addressable format exactly:

```
"<type> <size>\0<content>"
```

so `sha1("blob 5\0hello")` produces the same hash as real Git would for the same content. This means:

- **Identical content always produces the same hash** — deduplication is automatic
- **Any two objects with different content always get different hashes** (practically speaking) — the hash is the identity
- Commit IDs, tree IDs, and blob IDs are all content-derived — there is no separate counter or UUID

### 3. Branches as Pointers + Symbolic HEAD

Branches in `my_git` work exactly as they do in real Git:

- A branch is a **plain text file** under `.my_git/refs/<name>` containing a single commit hash. Moving a branch forward is just overwriting that file.
- `HEAD` is a **symbolic reference** — normally it contains `ref: refs/main` (or another branch name), meaning HEAD indirectly points to whatever commit `refs/main` points to. This is the attached state.
- In **detached HEAD** mode (`checkout <hash>`), `HEAD` contains the raw commit hash directly, not a `ref:` prefix. Committing in this state advances `HEAD` itself (not any branch), and any work done is "floating" — not on any named branch.

`get_current_branch_ref()` reads `HEAD`, detects the `ref:` prefix, and returns the ref path. `get_head_commit()` follows the chain: if `HEAD` is symbolic, it reads the ref file to get the commit hash; if detached, it returns the hash directly. `set_head_commit()` writes the new hash to the correct place — the ref file if attached, `HEAD` directly if detached. This means branch advancement on commit is automatic: committing while on `main` updates `refs/main`, not `HEAD` itself.

### 4. Diff Engine (LCS)

`cmd_diff` takes two commit hashes, reconstructs both snapshots via `reconstruct_commit`, and compares them file by file. For each filename that appears in either snapshot:

- **Only in commit A**: the file was deleted — all lines shown as `-`
- **Only in commit B**: the file was added — all lines shown as `+`
- **In both**: content is compared line-by-line using `diff_lines`

`diff_lines` uses a standard **Longest Common Subsequence (LCS)** dynamic programming algorithm:

1. `split_lines` splits each file's content into a `vector<string>` of lines
2. `build_lcs_table` builds an `(n+1) × (m+1)` DP table where `dp[i][j]` = length of LCS of the first `i` lines of A and first `j` lines of B. Each cell is computed in O(1) from its neighbors, giving O(n·m) total time
3. `diff_lines` backtracks through the DP table from `dp[n][m]` to `dp[0][0]`:
   - If `a[i-1] == b[j-1]` → unchanged line (` `), move diagonally
   - If `dp[i-1][j] > dp[i][j-1]` → deleted line (`-`) from A, move up
   - Otherwise → added line (`+`) from B, move left
4. Since backtracking produces lines in reverse order, the result vector is reversed before output

The approach is identical in principle to GNU `diff` and POSIX `diff`, just without optimizations for large files (e.g., patience diff, histogram diff).

### 5. Three-Way Merge & Conflict Detection

`cmd_merge` implements a real three-way merge, the same algorithm used by real Git for non-rebased merges:

**Step 1 — Find the merge base.**
`find_merge_base` performs BFS from the current HEAD, collecting all ancestor commit hashes into a set (`get_ancestors`). It then performs BFS from the tip of the branch being merged, returning the first commit that appears in the ancestor set. This is the **common ancestor** — the point where the two branches diverged.

**Step 2 — Reconstruct three snapshots.**

- **Base**: `reconstruct_commit(merge_base)` — what the file looked like when both branches last agreed
- **Ours**: `reconstruct_commit(head)` — what our branch changed it to
- **Theirs**: `reconstruct_commit(their_tip)` — what their branch changed it to

**Step 3 — Merge each file.**
For each filename appearing in any of the three snapshots:

| Base   | Ours    | Theirs  | Result                                                |
| ------ | ------- | ------- | ----------------------------------------------------- |
| X      | X       | Y       | Take Theirs (only they changed it)                    |
| X      | Y       | X       | Take Ours (only we changed it)                        |
| absent | present | present | Auto-merge if identical, conflict otherwise           |
| X      | Y       | Y       | Same change on both sides — take either (no conflict) |
| X      | Y       | Z       | **Conflict** — both sides changed differently         |

Conflicts are written inline using real Git's marker format:

```
<<<<<<< ours
our version of the line(s)
=======
their version of the line(s)
>>>>>>> theirs
```

**Step 4 — Write `MERGE_HEAD`.**
`MERGE_HEAD` is written with the hash of the commit being merged in (`theirs`). This file signals that a merge is in progress. When `cmd_commit` runs and detects `MERGE_HEAD`, it records both `parent: <ours>` and `parent2: <theirs>` in the commit metadata, creating a **two-parent merge commit**, and then deletes `MERGE_HEAD` to signal completion.

If there are no conflicts, the merged files are written to the working directory and the user is prompted to `add` and `commit` to finalize. If there are conflicts, the conflict-marked files are written and the user must resolve them manually before staging and committing.

### 6. Remotes (Push / Fetch / Pull / Clone)

`my_git` supports **two remote transports**, chosen automatically based on the registered URL: a **filesystem transport** (the original implementation, equivalent to real Git's `file://` protocol) and an **HTTP transport** (added in Phases 13–18, talking to a `my_git_server` instance). A remote is registered by writing a single line into `.my_git/config`:

```
remote.origin.url=C:/path/to/RemoteRepo
remote.origin.url=http://localhost:8080
```

`get_remote_url()` reads this config. For filesystem paths it resolves relative paths to absolute and normalizes backslashes to forward slashes; for `http://`/`https://` URLs it returns the string **unmodified** — an early bug had `fs::absolute()` mangling HTTP URLs by treating them as relative paths, since `std::filesystem` considers `"http://host:port"` a relative path (no drive letter, no leading slash). Both `cmd_remote_add` and `get_remote_url` now check `is_http_url()` first and skip path resolution entirely for HTTP.

Each of `cmd_push`, `cmd_fetch`, `cmd_pull`, and `cmd_clone` begins with an `if (is_http_url(remote_path)) { ... HTTP branch ... } else { ... filesystem branch (unchanged) ... }` split, so the filesystem behavior described below is byte-for-byte identical to earlier phases, with the HTTP behavior layered alongside it.

**Filesystem `push`** — walks the local commit chain backwards by following `parent:` links in each commit's metadata. For each commit encountered, it checks whether that commit already exists in the remote's `.my_git/commits/`. If not, it copies the entire commit folder (metadata + any associated objects) from local to remote using `copy_commits_recursive`. It also copies any new objects from local `.my_git/objects/` into remote's object store (skipping objects that already exist — deduplication). Finally it overwrites the remote's `refs/<branch>` with the new tip hash. If the remote's current tip is not an ancestor of the local tip, the push is rejected (fast-forward only).

**Filesystem `fetch`** — the reverse of push. Iterates over all branches in the remote's `refs/` directory, calls `copy_commits_recursive` from remote into local (same recursive logic, just swapped direction), and updates `.my_git/refs/remotes/<name>/<branch>` tracking refs to reflect what the remote currently has. Does **not** touch the working directory or HEAD — only downloads objects and updates remote-tracking refs.

**`pull`** (identical code for both transports) — a two-line operation:

```cpp
cmd_fetch(remote_name);
cmd_merge("remotes/" + remote_name + "/" + branch);
```

Fetch brings the commits down, merge integrates them into the current branch. After a conflict-free pull, the user stages and commits to record the merge commit (with `parent2` pointing at the fetched tip). Because `cmd_fetch` already branches internally on transport type and `cmd_merge` only ever reads local data, `cmd_pull` required **zero code changes** to work over HTTP — verified end-to-end during Phase 17 testing.

**Filesystem `clone`** — creates the destination directory, initializes it as a new repo (`cmd_init`), then copies the entire `.my_git/objects/` and `.my_git/commits/` database from source to destination using `fs::copy` with recursive + overwrite options. Writes `remote.origin.url=<source>` into the clone's config. Then reads the source's `HEAD` to determine the default branch, copies the corresponding ref, and checks out the working tree by reconstructing the tip commit's snapshot and writing all files to disk via `write_file` (creating any needed subdirectories). The result is a fully functional repo with full history and a configured `origin`. `staging/` is always created **empty** on clone (never copied from source) — staged-but-uncommitted work shouldn't carry over to a clone, matching real Git's behavior.

**HTTP `push`/`fetch`/`clone`** are described in detail in sections 13–14 below, alongside the server that makes them possible.

### 7. Commit Graph Visualization (Multi-Lane ASCII DAG)

`cmd_graph` renders the commit history as a multi-lane ASCII directed acyclic graph. The algorithm has several stages:

**Collect all commits** — walks every file in `.my_git/refs/` (including `remotes/`) to find all branch tips, then BFS-traverses the full commit graph to collect every reachable commit with its metadata (hash, parents, message, timestamp).

**Topological sort** — performs Kahn's algorithm (BFS with in-degree tracking) to produce a topological order where every commit appears before its children. Commits at the same topological level are sorted by timestamp to produce a stable, chronologically sensible ordering.

**Lane assignment** — each "active branch" in the graph occupies a lane (column). When a commit is encountered:

- Its primary parent continues in its lane
- A merge commit's second parent opens a new lane on the right
- When two lanes converge at a merge commit, the extra lane is closed

**ASCII rendering** — each commit line shows:

```
*  <short-hash>  [branch-label]  (HEAD)  "commit message"
```

Connectors between lines use `|`, `\`, `/`, and `-` characters to draw lane lines and fork/join transitions. Merge commits produce a fork-and-join pattern:

```
*        <hash> (HEAD) [main]  "Merge feature into main"
|--\
|    *   <hash> [feature]  "feature work"
|--/
*        <hash>  "base commit"
```

Branch labels are appended next to the commit that each branch tip points to, and `(HEAD)` marks the currently checked-out commit.

### 8. Object Database (Blobs, Trees, Compression)

The object database lives under `.my_git/objects/` and stores two types of objects — **blobs** (file content) and **trees** (directory listings). The on-disk format for each object is:

```
"<type> <size>\0" + [4-byte LE uint32: compressed_length] + zlib_compress(content)
```

The header (`type + size + null byte`) is stored uncompressed so `read_object` can parse the type and original size without decompressing first, then uses those to decompress exactly the right number of bytes. `write_object` checks if the object already exists before writing (deduplication — if two files in different commits have identical content, they share one object on disk).

**Blob** — stores raw file content. Hash input: `"blob <len>\0<content>"`.

**Tree** — stores a directory listing as newline-separated text entries:

```
100644 blob <hash> filename.txt
040000 tree <hash> subdirectory
```

`100644` is the mode for a regular file; `040000` for a subtree. Hash input: `"tree <len>\0<entries>"`.

`write_tree` (used by plumbing commands) builds a tree object from the current working directory. `build_tree_from_map` (used by `cmd_commit`) builds a tree from an in-memory `map<string, string>` of path → content, handling nested paths recursively. `load_tree_recursive` reverses this on `reconstruct_commit`, walking a tree object and returning the flat `map<string, string>` with full paths as keys.

`fsck` validates the entire object graph: for every commit it checks that the `tree:` hash in metadata refers to a real object, then recursively validates every blob and subtree hash referenced by that tree. Any missing or unresolvable hash is reported as an error.

### 9. Nested Directory Trees

Nested directory support required coordinated changes across `add`, `commit`, `checkout`, `status`, and the object store:

**`add` change** — `fs::path(".my_git/staging") / filename` (using the full relative path including subdirectory, not just `filename()`) so that staging `src/utils/sha1.cpp` writes to `staging/src/utils/sha1.cpp`. `fs::create_directories` ensures the subdirectory exists inside `staging/` before copying.

**`commit` change** — switched from flat `vector<string>` file lists to `map<string, string> snapshot` (path → content). `reconstruct_commit(parent)` already returns this exact type, so inheriting unchanged files from the parent is a one-liner. `build_tree_from_map(snapshot)` then handles the recursive tree construction.

**`build_tree_from_map`** — the core of nested tree support. Algorithm: split each path on its **first** `/`. Paths with no `/` are direct blobs — write a blob object and add a `100644 blob <hash> <name>` entry. Paths with a `/` are grouped by their first component into a `subdirs` map, each group then processed by a recursive call which returns a subtree hash — add a `040000 tree <hash> <dirname>` entry. This naturally handles arbitrary nesting depth: `a/b/c/file.txt` produces three nested tree objects.

A verified example of the resulting tree structure:

```
ls-tree <commit-tree-hash>:
  100644 blob <hash> file1.txt
  040000 tree <hash> nested_test
               └── 100644 blob <hash> hello.cpp
```

**`checkout` cleanup** — after removing a file at `nested_test/hello.cpp`, `remove_empty_dirs_upward` walks upward removing any now-empty parent directories, stopping at a configurable boundary. This ensures the `nested_test/` folder itself disappears when checking out a commit that predates it. The boundary parameter prevents it from accidentally deleting `staging/` itself after clearing out all staged files.

**`status` recursive scan** — upgraded from `fs::directory_iterator` to `fs::recursive_directory_iterator` so untracked and modified files inside subdirectories are detected correctly. Paths are normalized to forward-slash relative paths via `fs::relative(entry.path(), ".")`, matching the keys used by the snapshot map returned by `reconstruct_commit`.

### 10. .mygitignore Ignore System

`.mygitignore` is a file at the project root that lists patterns for files and directories that `my_git` should ignore — equivalent to real Git's `.gitignore`. It is read by `load_ignore_rules()` which strips blank lines and `#` comments and returns a `vector<string>` of raw pattern strings.

`matches_ignore_rule(relpath, rules)` supports three pattern types:

| Pattern form | Example          | Matches                                                            |
| ------------ | ---------------- | ------------------------------------------------------------------ |
| Trailing `/` | `build/`         | Any path whose first component is `build` (directory prefix match) |
| Leading `*.` | `*.exe`          | Any file whose name ends with `.exe` (extension glob)              |
| Exact string | `CMakeCache.txt` | Exact filename or exact relative path match                        |

Both `cmd_add` and `cmd_status` call these functions before processing any file. `.my_git/` is always implicitly ignored regardless of rules — it is hardcoded as the first check in `matches_ignore_rule`.

The project's own `.mygitignore` contains:

```
build/
.vscode/
.git/
*.exe
*.obj
*.o
*.ilk
*.pdb
.gitignore
.gitkeep
```

This means the project tracks its own source files and `CMakeLists.txt` but ignores compiled output, editor config, and real Git's internal folder.

### 11. Staging Area Lifecycle

The staging area is a two-part structure:

- **`.my_git/index`** — a plain text file with one staged filename per line. This is the list of what will be included in the next commit.
- **`.my_git/staging/`** — a directory that mirrors the working directory structure. When a file is staged, a copy is placed here (including subdirectories). This snapshot is what gets committed — not the working directory file — so changes made after `add` but before `commit` are correctly excluded.

**After `add`:**

- File is copied into `.my_git/staging/<path>` (creating subdirectories as needed)
- Filename is appended to `.my_git/index` (if not already present)

**After `commit`:**

- All staged files are read from `.my_git/staging/`, turned into blob objects, assembled into a tree, and recorded in the commit
- Each staged file is then deleted from `.my_git/staging/` via `fs::remove`
- `remove_empty_dirs_upward` cleans up any empty subdirectories left behind in `staging/`, stopping at the `staging/` boundary so the folder itself is never deleted
- `.my_git/index` is wiped to empty (`write_file(".my_git/index", "")`)

This means after a clean commit, `staging/` is always empty and `index` is always blank — a clean slate for the next round of `add` calls.

### 12. Codebase Modularization

The original single `main.cpp` (~2500 lines) has been split into three well-defined layers:

| Layer        | Location        | Contents                                                                                                                                                                                                                                    |
| ------------ | --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Utils**    | `src/utils/`    | Pure functions with zero git knowledge — SHA-1 hashing, file I/O primitives, timestamp formatting. No layer depends on these except to call them.                                                                                           |
| **Core**     | `src/core/`     | Git internals — object store (blobs/trees/compression), ref management (HEAD/branches), commit graph traversal (ancestors/merge-base), diff engine (LCS), ignore rules, remote URL resolution. These call into utils but not into commands. |
| **Commands** | `src/commands/` | One file per user-facing command. Each command function calls into core and utils as needed. Commands may call other commands (e.g. `cmd_pull` calls `cmd_fetch` then `cmd_merge`).                                                         |
| **Dispatch** | `src/main.cpp`  | Only `main()` — parses `argc`/`argv`, validates argument counts, and calls the appropriate `cmd_*` function. ~180 lines total.                                                                                                              |

All headers live under `include/` mirroring the `src/` structure. `CMakeLists.txt` uses `include_directories(include)` so every source file includes headers as `"utils/sha1.h"`, `"core/refs.h"`, `"commands/merge.h"` — no relative `../` path hacks needed.

Internal helper functions used only within a single command file (e.g. `check_tree_recursive` in `fsck.cpp`) are declared `static` and never exposed in any header — they are private to their translation unit.

### 13. HTTP Server (my_git_server)

`my_git_server` is a **second, independent executable**, built from `src/server/main.cpp`, sharing only a slice of `core/` with the CLI (`object_store.cpp` and `commits.cpp` — just enough to read/write objects and validate fast-forwards). It uses [cpp-httplib](https://github.com/yhirose/cpp-httplib), a single-header HTTP library vendored at `include/third_party/httplib.h`.

```powershell
my_git_server.exe 8080 path\to\repo
```

On startup the server validates that `path\to\repo\.my_git` exists, captures the **absolute** path for its startup message, then calls `fs::current_path(repo_root)` so every subsequent file operation can use the same relative paths (`".my_git/objects/..."`, etc.) as the CLI — meaning `object_store.cpp` and `commits.cpp` work completely unmodified inside the server, with zero awareness that they're running inside a different executable. (Capturing the absolute path _before_ the `current_path` call matters: doing it after caused a real bug where the path got prepended to itself, producing a doubled path in the startup message — purely cosmetic, since the actual `current_path` change had already succeeded, but confusing to read.)

The server is single-threaded-per-request via httplib's default thread pool and holds no in-memory state between requests — every route re-reads from disk, so the server can be killed and restarted at any time without losing anything (all durable state lives in `.my_git/` on disk, same as the CLI).

### 14. Read-Only Repository API

Four `GET` routes expose a repository's contents for reading, matching exactly what `cmd_fetch` and `cmd_clone` need to reconstruct a repo remotely:

| Route                | Reads from                                          | Returns                                                           |
| -------------------- | --------------------------------------------------- | ----------------------------------------------------------------- |
| `GET /refs`          | `.my_git/refs/` (recursively, including `remotes/`) | JSON array: `[{"name":"main","hash":"..."}]`                      |
| `GET /refs/<name>`   | `.my_git/refs/<name>`                               | raw commit hash (plain text), 404 if missing                      |
| `GET /commit/<hash>` | `.my_git/commits/<hash>/metadata`                   | raw metadata text, 404 if missing                                 |
| `GET /object/<hash>` | `.my_git/objects/<aa>/<rest>`                       | raw on-disk bytes (header + zlib-compressed body), 404 if missing |

`GET /object/<hash>` deliberately returns the **exact on-disk bytes**, not decompressed content — the client already knows how to parse `"<type> <size>\0" + [4-byte length] + compressed_body` via `read_object()`, so the server does zero compression/decompression work and stays a thin disk-reading layer. This symmetry matters later: `POST /push` (section 17 below, see Example Walkthrough) writes objects in this exact same raw format, so a pushed object is byte-identical to what a subsequent `GET /object/<hash>` would serve.

`/refs` uses a small hand-rolled JSON array builder (`list_refs_recursive`) rather than a JSON library — the response shape is fixed and simple enough that a dependency wasn't worth adding. The same pattern (hand-rolled JSON, no library) is used throughout the networking layer, including the client-side parsing in `http_client.cpp` and the request bodies for `POST /push` and `POST /have`.

**Client-side consumption (`core/http_client.cpp`)** mirrors these four routes one-to-one: `http_get_refs`, `http_get_ref`, `http_get_commit_metadata`, `http_get_object`. `http_fetch_commits` is the orchestrator — given a starting commit hash, it walks `parent`/`parent2` links (via `http_get_commit_metadata`, parsing the same `parent:`/`parent2:`/`tree:` metadata format used everywhere else in the codebase), and for each commit's `tree:` hash, recursively walks every blob/subtree referenced (`fetch_tree_recursive`, mirroring the local `load_tree_recursive` logic but fetching over HTTP instead of reading local disk). Every fetched commit and object is written to `.my_git/commits/` and `.my_git/objects/` in the identical on-disk format the filesystem transport produces — `object_exists()` is checked before each write, so a fetch never re-downloads something already present, and a fetch can be safely interrupted and resumed (each commit/object is durable the moment it's written).

`cmd_fetch` and `cmd_clone` both branch on `is_http_url(remote_path)` at their very top — the HTTP branch calls into `http_client.cpp`, the filesystem branch is the original unmodified logic from section 6. `cmd_clone`'s HTTP path additionally needs to discover _every_ branch (not just one), so it calls `http_get_refs` once and loops `http_fetch_commits` per branch, writing each as both a local branch ref (`refs/<branch>`) and a remote-tracking ref (`refs/remotes/origin/<branch>`) — `main` is preferred as the default branch if present, otherwise the first branch returned is used as a fallback.

**Push and the negotiation protocol** (`POST /push`, `POST /have`) extend the server beyond read-only. `POST /push` accepts a JSON body of `{branch, commits: [{hash, metadata}], objects: [{hash, data_base64}]}` — objects are base64-encoded (via `utils/base64.cpp`, a from-scratch standard-alphabet implementation, since httplib carries no base64 utility) since raw zlib-compressed bytes aren't valid JSON text. The server writes each object and commit to disk (skipping anything matching `object_exists()`/an existing metadata file), then runs the **same fast-forward check** as the filesystem transport — `find_merge_base(current_tip, new_tip) == current_tip` — before updating the branch ref, rejecting with HTTP `409` otherwise. `POST /have` lets the client ask "which of these candidate commit/object hashes do you already have?" _before_ building the push payload, so `http_push_branch` only uploads the genuine delta rather than guessing based on locally-reasoned ancestor chains (which can be wrong or incomplete if the client doesn't have the remote's full history cached) — this is the same negotiation idea real Git's `git-upload-pack`/`git-receive-pack` use before transferring a packfile.

---

## Example Walkthrough

### Basic workflow

```powershell
my_git init
echo "hello" > file1.txt
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

`graph` output:

```
*        <hash-D> (HEAD) [main]  "Merge feature into main"
|--\
|    *   <hash-C> [feature]  "add feature"
|--/
*        <hash-A>  "first commit"
Branches:
  feature  -> <hash-C>
  main     -> <hash-D>
```

### Remote workflow

```powershell
# Setup two repos
mkdir RemoteRepo
cd RemoteRepo && my_git init && cd ..
cd LocalRepo

# Register remote and push
my_git remote add origin ..\RemoteRepo
my_git push origin main         # copies all commits to RemoteRepo

# Clone
my_git clone RemoteRepo ClonedRepo
# ClonedRepo now has full history + config with origin -> RemoteRepo

# Add a new commit to LocalRepo and push
echo "update" > update.txt
my_git add update.txt
my_git commit "Add update"
my_git push origin main         # 1 new commit object copied

# From ClonedRepo — fetch then pull
cd ..\ClonedRepo
my_git fetch origin             # downloads new commit, updates remotes/origin/main
                                # working directory unchanged
my_git pull origin main         # fetch + merge: update.txt appears in working dir
my_git add update.txt
my_git commit "Merge pull from origin"
my_git log                      # shows full history including merge commit
```

### Object database walkthrough

```powershell
my_git hash-object file1.txt
my_git cat-file -p <hash>

my_git write-tree
my_git ls-tree <tree-hash>
# 100644 blob <hash> file1.txt

my_git commit "a commit"
my_git ls-tree <commit-tree-hash>
# metadata contains tree: <hash> — no files/ folder exists

my_git fsck        # validates entire commit/tree/blob graph, 0 errors
my_git selftest    # 5/5 checks passed
```

### Nested directory walkthrough

```powershell
mkdir nested_test
Set-Content -Encoding ascii -Value "int main(){}" nested_test\hello.cpp
my_git add nested_test/hello.cpp
my_git commit "add nested directory"

my_git ls-tree <commit-tree-hash>
# 100644 blob ...  file1.txt
# 040000 tree ...  nested_test       <- subdirectory as a tree entry

my_git ls-tree <nested_test-tree-hash>
# 100644 blob ...  hello.cpp

# Status detects nested untracked files
echo "new" > nested_test/new_file.txt
my_git status
# Untracked files:
#   nested_test/new_file.txt

# Status detects nested modifications
echo "changed" > nested_test/hello.cpp
my_git status
# Modified (Changes not staged for commit):
#   nested_test/hello.cpp

# Checkout to older commit -> nested_test/ disappears entirely
my_git checkout <older-hash>
# Checkout back -> nested_test/hello.cpp fully restored
my_git checkout main
```

### HTTP networking walkthrough

```powershell
# Terminal 1 — start the server, pointed at an existing repo
my_git_server.exe 8080 path\to\LocalRepo
# my_git server listening on port 8080
# Serving repository: C:\...\LocalRepo

# Terminal 2 — clone it over HTTP
my_git clone http://localhost:8080 ClonedRepo
# Cloning into 'ClonedRepo'...
# Default branch: main
# Done. N file(s) checked out.

cd ClonedRepo
my_git remote add origin http://localhost:8080
my_git log     # full history, fetched object-by-object over HTTP
my_git fsck    # validates the entire fetched graph, 0 errors

# Push a new commit back to the server
echo "networked" > net.txt
my_git add net.txt
my_git commit "Add net.txt over HTTP"
my_git push origin main
# POST /push: server validates fast-forward, writes objects + commit, updates ref

# Fetch + pull pick up server-side changes the same way as filesystem remotes
my_git fetch origin
my_git pull origin main
```

Direct API inspection (no my_git client needed):

```powershell
curl http://localhost:8080/refs
# [{"name":"main","hash":"a33fb0f4..."}]

curl http://localhost:8080/refs/main
# a33fb0f4d879509747ed4e7483b6f42245591da2

curl http://localhost:8080/commit/a33fb0f4d879509747ed4e7483b6f42245591da2
# message: ...
# timestamp: ...
# parent: ...
# tree: ...
```

---

## Known Limitations

- `push` is fast-forward only (both transports) — no force push or diverged history resolution
- After a conflict-free `pull` or `merge`, the user must manually `add` changed files and `commit` to record the merge commit — there is no auto-commit on clean merge
- HTTP transport has no authentication — any client that can reach the server can push (acceptable for local/trusted-network use, not for public exposure)
- HTTP transport is plain HTTP only — no TLS, despite `crypt32` being linked (present only because httplib's header references it, not actively used)
- `POST /push` and `POST /have` use hand-rolled JSON parsing tied to the exact shapes this codebase produces — not a general-purpose JSON parser, so malformed or differently-shaped requests aren't handled gracefully
- Binary file handling is untested
- `graph` is terminal ASCII only (no color, no pager)
- Tree object format is simplified text, not real Git's compact binary encoding
- `clone` does not copy `.mygitignore` from the source repo to the destination (true for both transports)

---

## Possible Future Work

- **Auto-commit on clean merge** — skip the manual `add` + `commit` step when there are no conflicts
- **`branch -d <name>`** — delete a branch
- **`remote list` / `remote remove`** — manage registered remotes
- **`reset --soft` / `reset --hard`** — move HEAD backward through history
- **`revert`** — create a new commit that undoes the changes of a specific commit
- **`stash`** — temporarily save and restore dirty working directory state
- **`tag`** — named, permanent references to specific commits
- **`clone` copies `.mygitignore`** — so the cloned repo has the same ignore rules as the source
- **Colored graph output** and pager integration for large histories
- **Binary tree format** matching real Git's compact binary encoding
- **HTTP auth** — shared-secret or token-based push authentication
- **TLS support** for the HTTP transport (currently plain HTTP only)
- **Packfile-style bulk transfer** — batch multiple objects into one compressed payload instead of one `GET /object/<hash>` per object, reducing per-request overhead on large histories
- **A real JSON library** to replace the hand-rolled parsing in `/push`, `/have`, and `http_client.cpp`, once request shapes grow more complex

---

## Tech Stack

C++20 · `std::filesystem` · `fstream` · `zlib` · `map`/`set`/`queue` · CMake · [cpp-httplib](https://github.com/yhirose/cpp-httplib) (vendored, header-only) · hand-written SHA-1 and base64
