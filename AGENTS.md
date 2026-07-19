# Agent guidance

## Working agreements

- Read `README.md` and `UPSTREAM.md` before changing the runtime.
- Check the current branch and `git status`, and preserve unrelated changes.
- Keep changes focused and do not edit the `vendor/StarlingMonkey` submodule.
- Prefer small, reviewable commits.
- Do not commit, push, or open a pull request unless explicitly asked.

## Verification

Run the checks relevant to the changed code:

```bash
cmake --preset dev
cmake --build --preset dev --target sturnkey
ctest --preset dev --output-on-failure
```

Run formatting checks when changing C++:

```bash
cmake --build --preset dev --target format-check
```

## Commits

Use Conventional Commits in English:

```text
<type>(<optional-scope>): <imperative summary>
```

For non-trivial AI-assisted commits, add:

```text
Assisted-by: Codex
```

## Pull requests

Before creating a pull request, read `.github/PULL_REQUEST_TEMPLATE.md` and
use it as a reference.
