# Git & GitHub Best Practices

Expert-level habits. Keep history clean, reviewable, and recoverable.

## Non-negotiables

- **Use `git mv` (and `git rm`) -- never move/rename with the OS.** Moving a file outside git detaches its history; `git mv` preserves blame, rename-detection, and diffs.
- **Commit per task, not per feature.** Each commit is one logical, self-contained change with a clear message. Commit incrementally -- a giant end-of-feature commit is impossible to review or bisect.
- **Address PR feedback with fixup commits, not force-push.** Run `git commit --fixup=<sha>` targeting the *specific commit that introduced the changed code*, not just the tip; find the `<sha>` with `git log`/`git blame`. If a review point touches several commits, make more than one fixup so each lands on its proper parent. The reviewer sees exactly what changed since last round. Squash only at the end, before merge, with `git rebase -i --autosquash <base>`, then update the pushed branch with `git push --force-with-lease` (never plain `git push --force`) so the rewrite refuses to clobber work that landed on the remote meanwhile.
- **Use `git worktree` when possible.** Check out a branch in a separate directory instead of stashing/switching, to review, hotfix, or build in parallel without disturbing your working tree: `git worktree add ../repo-hotfix hotfix-branch`.

## Commits

- Imperative subject lines <=50 chars ("Add", not "Added"); blank line; body explaining *why*, not *what*.
- Stage deliberately with `git add -p` to keep unrelated changes out of a commit.
- Never commit secrets, generated files, or commented-out code.

## Branches & rebasing

- One branch per unit of work; branch off an up-to-date `main`.
- `git pull --rebase` to avoid noise merge commits on your feature branch.
- Rebase your own unpushed/unshared branches to keep history linear. Never rebase shared/public history.
- `git push --force-with-lease` (not `--force`) on the rare occasion you must rewrite a pushed branch -- it refuses to clobber others' work.

## Pull requests

- Keep PRs small and focused -- easier to review, faster to merge.
- Open as draft while in progress; describe *why* and how to test in the body.
- Prefer squash-merge for messy branches, rebase-merge for clean per-task histories. Avoid merge commits unless the branch's structure matters.

## Recovery & safety

- **Tags are reserved exclusively for releases** (`vX.Y.Z`) -- never create a tag as a backup, checkpoint, or bookmark. When a backup is genuinely warranted -- only for *critical, high-risk refactors* (a sweeping multi-commit history rewrite), not routine rebases/resets/filters -- save it as a local branch: `git branch backup/<name> HEAD`, then `git reset --hard backup/<name>` to restore. Don't spam backup branches before ordinary rewrites; `git reflog` already covers those.
- `git reflog` is your undo history -- almost nothing is truly lost.
- `git stash` for quick context switches (or better, a worktree).
- `git bisect` to pin down the commit that introduced a bug.
- `.gitignore` early; never `git add -A` blindly.
