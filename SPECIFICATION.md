# Dashboard Project Specification

> **Status:** Living document\
> **Purpose:** This document captures the agreed architecture, design
> decisions, assumptions, and open questions for the project. It is
> intended to evolve as development progresses.

------------------------------------------------------------------------

# 1. Project Goal

Develop a desktop application that acquires measurement data from a
microcontroller over USB serial, validates and buffers the incoming
data, and visualizes it in a responsive engineering GUI.

Primary objectives:

-   Reliable binary communication
-   Deterministic frame-based acquisition
-   Responsive user interface
-   Modular and maintainable software architecture
-   Extensible code base

------------------------------------------------------------------------

# 2. Communication Protocol

## Packet format

Each packet consists of 12 bytes.

  Field                     Type     Size
  ------------------------- -------- ------
  Start word                uint16   2 B
  Sample number (0...199)   uint16   2 B
  Channel 1                 uint16   2 B
  Channel 2                 uint16   2 B
  Channel 3                 uint16   2 B
  Channel 4                 uint16   2 B

Current start word:

-   0x55AA

Current payload:

-   Four ADC channels
-   10-bit unsigned values (0...1023)

Future improvements:

-   CRC/checksum
-   Packet versioning
-   Timestamp field (optional)

------------------------------------------------------------------------

# 3. Host Software Architecture

Planned software stack:

-   Python
-   PySide6
-   PyQtGraph
-   NumPy
-   pySerial

Proposed modules:

-   Serial reader
-   Packet parser
-   Frame assembler
-   Frame storage
-   Plot controller
-   Main window

------------------------------------------------------------------------

# 4. Data Flow

Microcontroller

↓

USB Serial

↓

Serial Reader

↓

Packet Parser

↓

Frame Assembler

↓

Display Snapshot

↓

GUI

------------------------------------------------------------------------

# 5. Threading Model

Two-thread architecture:

GUI thread

-   Windows
-   Controls
-   Plot updates

Worker thread

-   Serial reception
-   Packet parsing
-   Frame assembly

Communication shall use Qt signals/slots.

------------------------------------------------------------------------

# 6. Buffer Management

Current concept:

-   Double buffer (ping-pong)
-   Two frames
-   200 samples per frame
-   Four channels

One frame is displayed while the other is filled.

Each completed frame is copied into a display snapshot before plotting.

------------------------------------------------------------------------

# 7. GUI Design

Planned features:

-   Four-channel plot
-   Zoom
-   Pan
-   Reset view
-   Channel enable/disable
-   Connection controls
-   Status bar

Desired behavior:

-   Display updates only after complete frames.
-   Current zoom/pan should remain unchanged when a new frame arrives.

------------------------------------------------------------------------

# 8. Future Extensions

Possible future work:

-   Trigger functionality
-   Frame recording
-   CSV export
-   Binary file logging
-   FFT analysis
-   Measurement cursors
-   Protocol CRC
-   Configuration dialog
-   Multiple acquisition devices

------------------------------------------------------------------------

# 9. Open Questions

To be discussed:

-   Final packet endianness
-   CRC implementation
-   Trigger concepts
-   Fixed vs. autoscaled axes
-   Multiple synchronized plots
-   Configuration persistence
-   Project coding style

------------------------------------------------------------------------

# Revision Notes

Use this section to summarize major architectural decisions.

  Date         Author          Summary
  ------------ --------------- ---------------------------------------
  2026-07-26   Initial draft   Created initial project specification
