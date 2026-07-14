# Overview

The scope of this plan is to add several new high level features to manage system orchestration and control:
- A system orchestrator layer to manage system configuration and mdoes of operation
- A new command handler module with serial and web-based command interface plug-ins
- Add unix time reference and unix time to timestamp conversion

# Don't lose track of...

- 
- Include extension of the config interface to apply unique configurations for photo vs. video camera modes, and handle switching between those when switching between modes of operation. Also note that they could be interleaved - we may have a scheduled window for video recording, and then a separate scheduled window for periodic photo capture

# Design Approach

# System Orchestration

- Camera modes of operation:
  - Still photo, manually triggered
  - Still photo, periodic capture
  - Still photo, scheduled real-time windows, periodic capture
  - Video recording, manually triggered start/stop with max length timeout
  - Video recording, manually triggered, fixed configurable duration
  - Video recording, motion triggered, fixed configurable duration
  - Video recording, scheduled real-time windows
  - Video recording, scheduled real-time windows, motion triggered, configurable duration


We can break these down into orthogonal dimensions:
| Dimension |	Options |
|-----------|---------|
| Capture type | Photo / Video |
| Trigger	| Manual (command) / Periodic (interval) / Scheduled (wall-clock window) / Motion |
| Duration control | Single shot (photo), Fixed duration, Max-length timeout, Unlimited (until stop command) |

# Calendar/Wall-clock time manager

- Set a Unix time offset reference from internal system clock to establish conversion from system time to wall-clock time
- Add conversion between system time to calendar/wall-clock time (date/time)
- Support scheduler to manage schedule video/photo recording windows
- TBD: Integration with existing time service
- Plumb time reference into folder/file naming scheme in video controller service

# Command interface

The command interface, combined with the scheduler, will dictate the mode of operation of the system. Additionally, the command interface can be used for configuration, to provide event triggers (take photo, start/stop recording, etc.), and other future purposes.

## Initial thoughts on approach

These are some initial thoughts on approach, we may utilize or deviate from this as appropriate based on the holistic design.

- Commands consist of a command code byte, which identifies the command, and an optional payload.
- Commands should be acknowledged back to the caller (serial command interface, web command interface, or other) with a success or error code
- Create a simple enum for this to represent command return options
  - Success
  - Unrecognized command code
  - Invalid arguments/payload
  - Unable to execute/invalid state
  - etc.

## Initial list of commands

- Set unix time
- Set config options
- Take still capture
- Start recording
- Stop recording

# Design decisions

- Command handling should be synchronous
  - When a command is received and processed it is executed in blocking fashion
  - Where relevant, the preference is to have commands initiate asynchronous execution, for example to start recording we should initiate the recording and then return execution control to the main loop, where video recording can proceed through regular ticking of the various control modules
- Need to extend configuration interface
  - Currently resolution and jpeg quality settings are not applied from config
  - We want to be able to independently select resolution and quality between photo and video modes
  - This needs to be plumbed from config to camera controller
  - We also need to manage camera setup when toggling between still photo and video recording
    - Note that these may be interleaved, that is we may have a scheduled window for recording, as well as a scheduled window for periodic still photo captures
  