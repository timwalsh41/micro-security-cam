---
description: "Execute a single step from a design/implementation plan with review"
name: "execute plan step"
argument-hint: "<plan-file> <step-number>"
agent: "agent"
---

# Execute a Plan Step

You are given a plan file and a step number. Your job is to review, validate, and implement **exactly one step** — nothing more. Review the instructions below, present the plan to the user along with any questions or concerns, and receive explicit written approval from the user after review before starting implementation.

**⛔ CRITICAL RULE: You MUST NOT write, create, edit, or delete any source code files until you have received an explicit affirmative response from the user approving implementation. This applies EVEN IF you found no issues in your review. Present your review, ask for approval, and wait for the answer.**

## Inputs

- **Plan file**: `{{argument}}` — the first input is the plan file path, the second input is the step (generally numbered, but may be referred to by semantic description or next unfinished item in a checklist))
- If the user does not provide both a plan file and step number, or if the instructions are unclear, ask for clarification before proceeding.

## Workflow

1. **Load and Parse the Plan:** Read the plan file in full and identify the requested step by number.
2. **Review Codebase Context:** Read the `AGENTS.md` file for project conventions and guidelines. Review `docs/SYSTEM_DESIGN.md` to understand the current implementation. Read all files that are **directly relevant** to implementing this step (source files, headers, tests, configs). Review the **last 2–4 commits** for recent changes that may affect or overlap with this step.
3. **Validate the Plan Step:** Before writing any code, critically assess whether the plan step makes sense given the current state of the codebase. Does this step align with the existing code architecture, naming conventions, and patterns Are there gaps in the plan — missing edge cases, undefined interfaces, unclear error handling? Does this step conflict with recent changes (from the last 2–4 commits)? Are any prerequisites from prior steps already satisfied in the codebase? If not, flag this. Are there technical concerns (hardware constraints, library limitations, API mismatches)? While the plan should establish architeture, composition, data structures, and interfaces, it may leave some implementation details open - identify those and include in your assessment and execution plan.
4. **Present Review:** Present your assessment as a numbered list. Regardless of whether you found issues or not, **STOP here. Do not write any code.** Your message must end with an explicit question asking the user to approve implementation (e.g., "Do you approve proceeding with implementation?").
5. **WAIT for approval:** You must receive an explicit affirmative response from the user (e.g., "yes, proceed", "approved", "go ahead") before writing any code.
6. **Implement:** Once you have received explicit approval, implement **only** the single step requested. If relevant, update documentation (`docs/SYSTEM_DESIGN.md`, `README.md`) and add/edit tests as needed. Follow all project conventions from `AGENTS.md`.
6. **Summary:** After implementation, provide a brief summary: list each file modified or created, what was done in ~one sentence per file, note any tests added or updated, and if the plan has more steps, list the next one so the user knows where to pick up.
