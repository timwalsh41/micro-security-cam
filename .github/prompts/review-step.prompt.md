---
description: "Review implementation of a plan step against its design"
name: "review plan step"
argument-hint: "<plan-file> <step-number>"
agent: "agent"
---

# Review a Plan Step Implementation

You are given a plan file and a step number. Your job is to perform an **independent review** of the implemented changes for that step — verifying that the implementation is faithful to the design, correct, and meets project quality standards. You are NOT implementing anything; this is a read-only review.

## Inputs

- **Plan file**: `{{argument}}` — the first input is the plan file path, the second input is the step (generally numbered, but may be referred to by semantic description or most recent completed item in a checklist).
- If the user does not provide both a plan file and step number, or if the instructions are unclear, ask for clarification before proceeding.

## Workflow

1. **Load the Plan:** Read the plan file in full and identify the requested step. Note its design intent, acceptance criteria, and dependencies on prior steps.
2. **Gather Implementation Context:** Read `AGENTS.md` for project conventions. Review uncommitted changes as well as the last 2-3 commits to identify what changes were made in the implementation. Read all source, header, and test files relevant to this step — identified from the plan, git diffs, or includes in changed files.
3. **Review Against the Design:** Compare the implementation to the plan step. Is it faithful to the spec or are there unjustified deviations? Is it complete — edge cases, error handling, interface contracts? Is the logic correct — any bugs, race conditions, resource leaks?
4. **Review Code Quality:** Verify it meets guidelines in `AGENTS.md`, and ensure there is no dead code or leftover TODOs.
5. **Review Code Style:** Ensure it matches the conventions established in `AGENTS.md`.
6. **Review Test Coverage:** Assess whether native tests (`test/native/`) cover all logical paths, edge cases, and error conditions. Check for hardware tests (`test/hardware/`) if applicable. Identify any gaps in coverage.
7. **Run ALL Tests:** Activate the environment with `source ~/.platformio/penv/bin/activate`, then run `pio test -e native` and `pio test -e seeed_xiao_esp32s3_test`. Report pass/fail counts for each. If any test fails, provide details and whether it is related to this step's changes. To reiterate, make sure **ALL** tests pass, not just new/changed tests.
8. **Present Findings:** Organize as: (1) **Design Fidelity** — does the implementation match the plan? (2) **Issues Found** — numbered list with severity (Critical/Major/Minor/Nit), description, location, and suggestion. (3) **Test Coverage** — summary and gaps. (4) **Test Results** — pass/fail counts. (5) **Verdict** — Approved, Approved with minor suggestions, or Changes requested.
