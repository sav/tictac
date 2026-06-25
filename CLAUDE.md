# CLAUDE.md

## Code Style

- Always use C++23.
- Avoid unnecessary comments.

## Building

- Don't build after every change. Build only when explicitly asked. When you do build, fix any errors that come up.

## Git

- Never use the `Co-Authored-By` tag in commits. Use `Assisted-By: <Model>` instead.
- Name branches as `claude/<branch>`, typically after the feature.
- After addressing review comments, commit the changes as a fixup and push to the remote branch.

## Working Together

- A question is not a request to change code. I often just discuss the code without wanting edits. I'll explicitly ask when I want a change; when in doubt, ask before touching anything.
- When addressing PR review comments, don't accept them blindly. If a comment is a question, answer it and justify your decision rather than immediately changing code. Change it only if you agree, or if the reviewer explicitly tells you to after your reply. The reviewer may raise several questions across a thread, so if another one comes up, keep making your case rather than caving.

