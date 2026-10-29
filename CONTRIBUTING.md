# Contributing to DeskTick

Thanks for wanting to help. DeskTick is small and deliberately minimal, so a short read here saves us both time.

## Before you start

For anything bigger than a bug fix - a new face, a new option, a behaviour change - open an issue first and describe what you have in mind. It's the quickest way to find out whether it fits before you spend time on it.

## AI-assisted contributions

Using AI tools (Copilot, Codex, Claude, ChatGPT, ...) is fine. But:

- **You are the author.** You're accountable for every line in the pull request, whoever or whatever typed it.
- **Say so.** In the PR description, name the tool and roughly how much of the change it wrote.
- **Read the whole diff yourself.** You should be able to explain any line of it if asked.
- **Test it yourself, by hand** (see below). An agent's summary of its own testing is not testing.

Pull requests that look generated and unreviewed - changes that miss obvious cases, comments that don't match the code, unrelated files along for the ride - may be closed without a line-by-line review.

## Testing

There is no test suite; verification is manual.

- Build Release x64 with Visual Studio 2026 (toolset v145). The build must be warning-free at `/W4`.
- Run the exe and exercise what you changed.
- If your change touches faces, drawing, hit-testing or resizing, check **every built-in face**: in normal and resize mode, with its options toggled (date, panel, seconds, and so on), at a small and a large size.
- Say in the PR what you checked.

## Code

- **Match the surrounding code**, comments included. Comments here explain *why* something is done the way it is. Keep that, and update a comment when you change the reasoning behind it.
- **No new dependencies.** Raw Win32 and Direct2D; no STL outside `settings.cpp`.
- **Non-ASCII in a string literal must be a `\u` escape**, never the character itself. Source files have no BOM, so a pasted character turns into mojibake.
- **Any text shown to the user goes in `src\DeskTick\Localization\translations.csv`**, then run `Localization\build.ps1`. Never hand-edit `strings.h` or `strings.rc`.
- **Keep the pull request to one change.** No unrelated refactors, test harnesses or README edits riding along.

## Contributor License Agreement

By submitting a pull request you agree to the [Contributor License Agreement](CLA.md); you keep the copyright to your work. New pull requests have a checkbox for it. If yours was opened without one, reply on the PR with:

> I have read and agree to the DeskTick Contributor License Agreement (CLA.md), and it applies to all my contributions in this pull request, including commits made before it was added.

If your employer may have rights to work you create, make sure you have permission to contribute it.
