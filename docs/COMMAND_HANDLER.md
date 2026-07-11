# Overview

The scope of this plan is to add a new command handler module and serial command interface, with hooks to later add in web-based command handling.

# Approach

## Architecture

## Command interface

- Commands consist of a command code byte, which identifies the command, and an optional payload.
- Commands should be acknowledged back to the caller (serial command interface, web command interface, or other) with a success or error code
- Create a simple enum for this to represent command return options
  - Success
  - Unrecognized command code
  - Invalid arguments/payload
  - Unable to execute/invalid state
  - etc.

## Design decisions

Command handling should be synchronous - when a command is received and processed it is executed in blocking fashion. Thus commands should be fairly simple and fast to execute, for example to start recording we should initiate the recording and then return execution control to the main loop, where video recording can proceed through regular ticking of the various control modules. 

# List of commands

- Set unix time
- Set config options
- Take still capture
- Start recording
- Stop recording
