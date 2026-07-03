# Git & GitHub Best Practices

Expert-level habits. Keep history clean, reviewable, and recoverable.

## Non-negotiables

- **Use `git mv` (and `git rm`) — never move/rename with the OS.** Moving a file outside git detaches its history; `git mv` preserves it so blame, rename-detection, and diffs stay intact.
- **Commit per task, not per feature.** Each commit is one logical, self-contained change with a clear message. Commit incrementally as you go — a giant end-of-feature commit is impossible to review or bisect.
- **Address PR feedback with fixup commits, not force-push.** For each review point, run `git commit --fixup=<sha>` targeting the *specific commit that introduced the code being changed* — not just the tip. Use `git log`/`git blame` to find the right `<sha>`. If a review point touches code from several commits, create more than one fixup so each lands on its proper parent. The reviewer sees exactly what changed since last round. Squash them only at the end with `git rebase -i --autosquash <base>` before merge, then update the pushed branch with `git push --force-with-lease` (never plain `git push --force`) so the rewrite refuses to clobber any work that landed on the remote in the meantime.
- **Use `git worktree` when possible.** Check out a branch in a separate directory instead of stashing/switching. Lets you review, hotfix, or build in parallel without disturbing your working tree: `git worktree add ../repo-hotfix hotfix-branch`.

## Commits

- Write imperative subject lines ≤50 chars ("Add", not "Added"); blank line; body explaining *why*, not *what*.
- Stage deliberately with `git add -p` to keep unrelated changes out of a commit.
- Never commit secrets, generated files, or commented-out code.

## Branches & rebasing

- One branch per unit of work; branch off an up-to-date `main`.
- `git pull --rebase` to avoid noise merge commits on your feature branch.
- Rebase your own unpushed/unshared branches to keep history linear. Never rebase shared/public history.
- `git push --force-with-lease` (not `--force`) on the rare occasion you must rewrite a pushed branch — it refuses to clobber others' work.

## Pull requests

- Keep PRs small and focused — easier to review, faster to merge.
- Open as draft while in progress; describe *why* and how to test in the body.
- Prefer squash-merge for messy branches, rebase-merge for clean per-task histories. Avoid merge commits unless the branch's structure matters.

## Recovery & safety

- `git reflog` is your undo history — almost nothing is truly lost.
- `git stash` for quick context switches (or better, a worktree).
- `git bisect` to pin down the commit that introduced a bug.
- `.gitignore` early; never `git add -A` blindly.
