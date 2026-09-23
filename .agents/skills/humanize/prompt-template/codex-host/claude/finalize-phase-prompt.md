# Finalize the Codex implementation/review loop

The independent Codex code review passed. Preserve the reviewed code unchanged.
Do not introduce refactoring or other source changes after this review.

1. Report the changes and validation results, including any limits on validation.
2. Write the final summary to {{FINALIZE_SUMMARY_FILE}}.
3. Stop normally so the Humanize Stop hook can complete the loop.

The original plan is {{PLAN_FILE}} and the goal tracker is {{GOAL_TRACKER_FILE}}.
If new code changes are necessary, explain why and start a new review loop after
the current loop completes, rather than treating unreviewed changes as approved.
