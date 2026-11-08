---
name: commit
description: Prepare and make a git commit in the umod4 repo, following Robin's rules. Use when the user asks to commit, check in, draft or write a commit message, stage files, or prepare changes for check-in. Also use for questions about committing, such as "what should I do before the next commit", "ready to commit?", "what is left before I check in", or "what has changed / is uncommitted". Drafting a message is NOT permission to commit.
---

# Committing in umod4

The owner is new to git and has been burned by unasked commits twice.
These rules are strict.

## The three rules that matter most

1. **Never run `git commit` without an explicit "yes, commit" for that specific commit.**
   - Staging (`git add`) and showing diffs are fine without asking.
   - "Draft the commit message" is a request for text, not for a commit.
   - An answer to an adjacent question (for example "one commit or several?") is not authorization.
   - When unsure, show the drafted message and ask "Should I commit this?" as its own question.
2. **Ask "Have you run the test runner?" before proceeding.**
   - Run order and commands are in the hardware-test-run skill (`uv run tests/runner.py ...`).
   - If the change only touches docs, tools or the visualizer, say so and let the user decide whether a hardware run is needed.
3. **Never `git add -A` or `git add .`. Stage files by name.**
   - `doc/__investigations/` holds private lab notes in a public repo. They are deliberately untracked and must never be committed or pushed.

## Procedure

1. Show the state: `git status` and `git log --oneline -5`.
2. List what is untracked or modified, and propose the exact files to stage.
   - Watch for files that should not go in: lab notes, `build/`, scratch files, large binaries or images.
   - If a change replaces a file (for example a renamed image), check that references point at the new name and that the old file is `git rm`'d.
3. Ask the test-runner question (rule 2).
4. Check whitespace: `git diff --check` (no trailing whitespace is a repo rule; tab width is 4).
5. Stage the named files, then show `git diff --cached --stat`.
6. If a change touches `lib/inc/log_ids.h`, confirm the decoder (`tools/logtools/decoder/decodelog.py`) was updated in the same commit.
7. Draft the message (format below) and show it.
8. Ask "Should I commit this?" Wait for a clear yes.
9. After committing, run `git log --oneline -3` and `git status` so the result is visible.
10. If the session produced new files under `doc/__investigations/`, remind the user that `backup-umod4` is run manually and protects those notes.

## Commit message format

- Short imperative subject line.
- Body: terse bullets grouped under category headers such as `WP:`, `Test harness:`, `Docs:`, `Tools:`. One theme per bullet, not one per file. The diff shows the files.
- Keep the reasons and any surprises.
- End with a one-line `Verified:` note saying what was checked, and what was NOT exercised.
- Roughly 10 to 15 lines total. Simple changes can be shorter.
- Finish with the attribution line the session instructs you to add.
- Do not reformat or amend past commits unless asked.

## Other git operations

Explain in plain language what any git operation will do before running it.

- **Branches:** before creating one, run `git branch --no-merged main` and warn about unmerged branches that should land first.
- **Merge or rebase:** say in one sentence what will happen. If conflicts appear, explain why they exist, not only how to resolve them.
- **Destructive commands** (`reset --hard`, `rebase`, `branch -D`, `push --force`): describe exactly what would be lost and get confirmation first.
- **Push:** never, unless asked.

## Keeping this skill current

At the end of a session that used this skill, if you hit a situation not covered here, propose a specific edit and wait for approval.
Never edit this file unasked.
