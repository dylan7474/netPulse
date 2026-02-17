# AGENTS.md

Guidance for future coding agents working in this repository.

## Repository overview

- This is a static web app project.
- Main application file: `index.html`.
- Project-level docs and build helpers live at repository root.

## Working rules

- Keep changes small and focused.
- Prefer updating documentation when behavior or controls change.
- Avoid introducing build systems beyond the existing lightweight `Makefile` unless requested.
- Preserve zero-dependency local development where possible.

## Validation checklist

When you modify docs or tooling scripts:

1. Run `./configure`.
2. Run `make verify`.
3. Ensure scripts intended to be executable have execute permissions.

When you modify UI behavior:

1. Confirm `index.html` still loads in a browser.
2. Update `README.md` controls/usage sections if needed.

## Commit and PR hygiene

- Use descriptive commit messages prefixed by area, e.g. `docs:` or `build:`.
- Summarize user-visible changes and validation commands in PR descriptions.
