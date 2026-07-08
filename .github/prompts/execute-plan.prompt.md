---
description: "Execute a single step from a design/implementation plan with review"
name: "execute plan step"
argument-hint: "<plan-file> <step-number>"
agent: "agent"
---

# Execute a Plan Step

You are given a plan file and a step number. Your job is to review, validate, and implement **exactly one step** — nothing more. Review the instructions below, present the plan to the user along with any questions or concerns, and receive explicit written approval from the user after review before starting implementation.

## Inputs

- **Plan file**: `{{argument}}` — the first input is the plan file path, the second input is the step (generally numbered, but may be referred to by semantic description or next unfinished item in a checklist))
- If the user does not provide both a plan file and step number, or if the instructions are unclear, ask for clarification before proceeding.

## Workflow

1. **Load and Parse the Plan:** Read the plan file in full and identify the requested step by number.
2. **Review Codebase Context:** Read the `AGENTS.md` file for project conventions and guidelines. Review `docs/SYSTEM_DESIGN.md` to understand the current implementation. Read all files that are **directly relevant** to implementing this step (source files, headers, tests, configs). Review the **last 2–4 commits** for recent changes that may affect or overlap with this step.
3. **Validate the Plan Step:** Before writing any code, critically assess whether the plan step makes sense given the current state of the codebase. Does this step align with the existing code architecture, naming conventions, and patterns Are there gaps in the plan — missing edge cases, undefined interfaces, unclear error handling? Does this step conflict with recent changes (from the last 2–4 commits)? Are any prerequisites from prior steps already satisfied in the codebase? If not, flag this. Are there technical concerns (hardware constraints, library limitations, API mismatches)?
4. **Raise Issues (if any):** If you find inconsistencies, gaps, or concerns stop and present them clearly as a numbered list. For each issue, explain *why* it's a concern and *suggest a resolution*. Do not proceed with implementation until the user confirms how to resolve the issues. If issue resolution results in changes to the planned approach, update the plan document accordingly.
5. **Implement:** Once any concerns are addressed with the user, implement **only** the single step requested — do not continue to subsequent steps. If relevant, update documentation (`docs/SYSTEM_DESIGN.md`, `README.md`) and add/edit tests as needed. Follow all project conventions from `AGENTS.md`.
6. **Summary:** After implementation, provide a brief summary: list each file modified or created, what was done in ~one sentence per file, note any tests added or updated, and if the plan has more steps, list the next one so the user knows where to pick up.
