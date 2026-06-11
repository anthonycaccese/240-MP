## Summary

<!-- What does this PR do? Link any related issue, e.g. "Closes #12". -->

## AI involvement

<!--
Required. Per CONTRIBUTING.md, please describe the scope of any AI use: which parts (if
any) were AI-generated, and what human review/testing you did before submitting.
Please write "No AI used" if that's the case. PRs that omit this may be closed without review.
-->

## Testing

<!-- How did you verify this? Single-platform testing is fine — just say which. -->

- Platform(s) tested:
- Display / output tested:

## Checklist

- [ ] Changes work with remote-only navigation (up/down/left/right, enter, esc/backspace)
- [ ] Handled sizing and positioning using the `root.sh` & `root.sw` properties (did not hardcode pixels) and kept CRT overscan in mind
- [ ] Did not add tracking or analytics; only wrote settings to the local data directory if the change required storage
- [ ] Follows the patterns in [ARCHITECTURE.md](https://github.com/anthonycaccese/240-MP/blob/main/ARCHITECTURE.md) and the principles in [CONTRIBUTING.md](https://github.com/anthonycaccese/240-MP/blob/main/CONTRIBUTING.md)
