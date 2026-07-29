# High-Level Architecture Specification

## 1. Purpose

This document defines the high-level software architecture for a PySide6-based application that receives four-channel `int16` measurement data over a serial interface and renders the data in a graphical user interface.

The architecture is designed to:

- Separate serial communication from rendering
- Enable independent development of the serial receiver and UI
- Prevent the serial receiver from blocking the GUI
- Provide deterministic producer–consumer handshaking
- Minimize shared mutable state
- Keep business logic independent of the thread-management implementation

---

## 2. System Overview

The system consists of three primary runtime components:

1. **SerialReader**
   - Receives serial data
   - Decodes packets
   - Assembles complete frames
   - Acts as the producer

2. **FrameBuffer**
   - Owns two shared frame buffers
   - Controls buffer ownership and publication
   - Implements producer–consumer synchronization
   - Acts as the shared data exchange object

3. **Renderer**
   - Retrieves newly published frames
   - Copies the frame into private memory
   - Updates the graphical display
   - Acts as the consumer

A shared configuration module defines the frame layout and timing parameters used by all components.

---

## 3. High-Level Architecture

```text
                         Main / GUI Thread
              +-----------------------------------+
              | MainWindow                        |
              | Renderer                          |
              | PySide6 / plotting widgets        |
              +------------------+----------------+
                                 ^
                                 |
                     copy published frame
                                 |
                     shared FrameBuffer object
                                 |
                     publish completed frame
                                 |
                                 v
              +------------------+----------------+
              | SerialReader QObject              |
              | running inside dedicated QThread  |
              +-----------------------------------+
                                 ^
                                 |
                         Serial interface
```

The `SerialReader` and `Renderer` hold references to the same `FrameBuffer` instance.

The `FrameBuffer` does not run in its own thread. It is a passive shared object whose methods execute in the thread that calls them.

---

## 4. Thread Model

### 4.1 GUI Thread

The main Qt thread contains:

- `QApplication`
- `MainWindow`
- `Renderer`
- All PySide6 widgets
- All plot updates

All GUI modifications shall occur in the GUI thread.

### 4.2 Serial Worker Thread

The `SerialReader` runs in a dedicated `QThread`.

The recommended Qt structure is:

```text
QThread
    └── SerialReader QObject
```

The `SerialReader` contains the serial communication and frame assembly logic. Thread creation, thread startup and thread shutdown are handled by the application setup code rather than by the `SerialReader` itself.

### 4.3 FrameBuffer Execution Context

The `FrameBuffer` has no execution context of its own.

- Producer-side methods execute in the serial worker thread.
- Consumer-side methods execute in the GUI thread.
- Internal synchronization protects shared state.

---

## 5. Shared Configuration

A shared configuration module defines immutable system parameters visible to both producer and consumer.

Typical parameters are:

| Parameter | Purpose |
|---|---|
| `CHANNEL_COUNT` | Number of measurement channels |
| `SAMPLES_PER_FRAME` | Number of samples contained in one complete frame |
| `SAMPLE_INTERVAL_S` | Time interval between consecutive samples |
| `SAMPLE_DTYPE` | Sample data type, specified as `int16` |
| `FRAME_DURATION_S` | Derived frame duration |

The frame duration is derived from:

```text
FRAME_DURATION_S = SAMPLES_PER_FRAME × SAMPLE_INTERVAL_S
```

The current system uses a frame duration of approximately two seconds.

Global configuration constants are acceptable because they are immutable.

Global mutable runtime objects shall be avoided.

---

## 6. Data Model

Each complete frame contains only measurement values.

No timestamp is stored in the frame.

The frame layout is:

```text
(SAMPLES_PER_FRAME, CHANNEL_COUNT)
```

The data type is:

```text
int16
```

Each row represents one sample.

Each column represents one channel.

Conceptually:

```text
frame[sample_index, channel_index]
```

For four channels:

```text
sample 0: channel 0, channel 1, channel 2, channel 3
sample 1: channel 0, channel 1, channel 2, channel 3
...
```

---

## 7. FrameBuffer Design

### 7.1 Purpose

The `FrameBuffer` is the only shared runtime object used for exchanging measurement data between producer and consumer.

It is responsible for:

- Owning two frame buffers
- Exposing the current write buffer to the producer
- Publishing completed frames
- Switching producer ownership between the two buffers
- Notifying the consumer when a new frame is available
- Returning a private copy of the newest frame
- Tracking frame sequence numbers

### 7.2 Shared Object Definition

```python
class DoubleFrameBuffer:
    """
    Thread-safe double buffer with one producer and one consumer.

    Internal state:

    _buffers[2]
        Two NumPy arrays containing frame data.

    _write_index
        Index of the buffer currently owned by the producer.

    _published_index
        Index of the most recently published frame.

    _frame_sequence
        Monotonically increasing frame counter.

    _condition
        Synchronization object used to notify the consumer when
        a newer frame becomes available.
    """
```

### 7.3 Internal Parameters

#### `_buffers`

Contains two NumPy arrays.

Each array has the shape:

```text
(SAMPLES_PER_FRAME, CHANNEL_COUNT)
```

Each element has type:

```text
int16
```

#### `_write_index`

Identifies the buffer currently owned by the producer.

Only the producer may modify this buffer.

After publication, the write index switches to the other buffer.

#### `_published_index`

Identifies the newest completed and published frame.

The consumer copies data from this buffer.

#### `_frame_sequence`

Monotonically increasing counter assigned to each published frame.

Purposes:

- Detect newly published frames
- Prevent duplicate processing
- Detect skipped frames
- Distinguish buffer content from buffer identity

The first published frame has sequence number `0`.

#### `_condition`

Synchronization primitive used to:

- Notify the consumer when a frame has been published
- Allow the consumer to wait without polling
- Combine notification and frame-sequence checking safely
- Avoid lost notifications

---

## 8. Ownership Model

The handshaking is based on explicit buffer ownership.

### 8.1 Producer Ownership

The producer owns the buffer referenced by `_write_index`.

The producer may:

- Write decoded samples into the current write buffer
- Fill the complete frame
- Publish the frame

The producer shall not:

- Modify the published buffer
- Continue using an old write-buffer reference after publication
- Access consumer-owned private copies

### 8.2 Consumer Ownership

The consumer never owns a shared buffer directly.

The consumer may:

- Wait for a newer frame
- Copy the published frame
- Render from the private copy

The consumer shall not:

- Modify either shared buffer
- Retain a reference to an internal shared NumPy array
- Render while holding the FrameBuffer synchronization lock

---

## 9. Producer–Consumer Handshaking

### 9.1 Handshake Principle

The producer and consumer do not communicate directly.

The producer publishes a completed frame through the `FrameBuffer`.

The consumer receives notification and retrieves the newest frame through the `FrameBuffer`.

Qt signals may be used as a GUI notification mechanism, but the frame data itself remains owned and managed by the `FrameBuffer`.

### 9.2 Producer Sequence

1. Request the current write buffer.
2. Receive serial packets.
3. Decode the packet contents.
4. Store the four channel values at the correct sample position.
5. Continue until all samples of the frame have been written.
6. Publish the completed frame.
7. Increment the frame sequence number.
8. Mark the completed buffer as published.
9. Switch producer ownership to the other buffer.
10. Notify the consumer.
11. Request the new write buffer.
12. Continue receiving the next frame.

### 9.3 Consumer Sequence

1. Wait for notification that a newer frame is available.
2. Compare the published frame sequence with the last processed sequence.
3. Confirm that the frame is new.
4. Copy the published frame into a private buffer.
5. Store the new frame sequence as the last processed sequence.
6. Release synchronization.
7. Render the private copy.
8. Wait for the next frame.

### 9.4 Buffer Switching Sequence

```text
Frame 0:
Producer writes Buffer 0
Producer publishes Buffer 0
Producer switches to Buffer 1
Consumer copies Buffer 0

Frame 1:
Producer writes Buffer 1
Producer publishes Buffer 1
Producer switches to Buffer 0
Consumer copies Buffer 1

Frame 2:
Producer writes Buffer 0
...
```

Because one frame requires approximately two seconds to fill, the consumer has ample time to copy the published buffer before that buffer is reused.

---

## 10. Frame Counter and Race-Condition Protection

The frame sequence counter is the authoritative indicator of whether a frame is new.

The notification mechanism only indicates that the consumer should check the shared state.

Without a frame counter, the following logical errors could occur:

- The consumer processes the same frame twice
- A notification is cleared after a newer frame was already published
- A newer frame exists but the notification state no longer reflects it
- The consumer cannot determine whether one or multiple frames were skipped

A new frame is available when:

```text
published_sequence > last_processed_sequence
```

Skipped frames can be detected using:

```text
skipped_frames =
    published_sequence - last_processed_sequence - 1
```

The frame counter does not protect the shared memory by itself. Memory safety is provided by:

- Double buffering
- Ownership rules
- Synchronization during publication and copying
- Rendering from a private copy

---

## 11. Synchronization Strategy

A condition variable is preferred over a plain event.

The condition variable allows the consumer to wait for the predicate:

```text
published_sequence > last_processed_sequence
```

This prevents:

- Lost notifications
- Duplicate processing
- Ambiguous event state
- Unnecessary polling

The synchronization lock shall be held only while:

- Updating buffer indices
- Updating the frame sequence
- Publishing a frame
- Copying the published frame

The lock shall not be held while:

- Receiving serial bytes
- Decoding packets
- Filling the write buffer
- Rendering
- Updating GUI widgets

---

## 12. Component Responsibilities

### 12.1 SerialReader

The `SerialReader` is responsible for:

- Opening and closing the serial port
- Receiving serial bytes
- Detecting packet boundaries
- Validating packet structure
- Decoding channel values
- Detecting frame completion
- Filling the current FrameBuffer write buffer
- Publishing completed frames
- Reporting communication errors and status changes

The `SerialReader` shall not:

- Update GUI widgets
- Render plots
- Depend on rendering classes
- Create or manage its own `QThread`

### 12.2 Renderer

The `Renderer` is responsible for:

- Receiving new-frame notification
- Requesting the latest published frame
- Copying the frame into private memory
- Updating plots
- Updating GUI status information
- Tracking the last processed sequence number

The `Renderer` shall not:

- Read from the serial port
- Decode serial packets
- Modify the shared frame buffers
- Perform GUI updates outside the GUI thread

### 12.3 FrameBuffer

The `FrameBuffer` is responsible for:

- Buffer ownership
- Buffer switching
- Frame publication
- Sequence tracking
- Synchronization
- Safe frame copying

The `FrameBuffer` shall not:

- Decode serial data
- Render data
- Create threads
- Own GUI objects

---

## 13. Qt Signaling

Qt signals are used for thread-safe notification between the serial worker thread and the GUI thread.

Recommended signal categories are:

| Signal | Purpose |
|---|---|
| `frame_published` | Notify the GUI that a new frame is available |
| `connection_changed` | Report serial connection state |
| `error_occurred` | Report communication or protocol errors |
| `finished` | Report worker shutdown completion |

The `frame_published` signal should carry only lightweight information, such as the frame sequence number.

The complete NumPy frame shall not be transferred through the Qt signal. The renderer retrieves the data from the shared `FrameBuffer`.

---

## 14. Object Construction and Dependency Injection

The application startup code creates one shared `FrameBuffer` instance.

The same object reference is passed to both components:

```text
SerialReader(FrameBuffer reference)
Renderer(FrameBuffer reference)
```

This design is preferred over global mutable variables because it provides:

- Explicit dependencies
- Easier unit testing
- Easier replacement with simulated components
- Clear object ownership
- Lower coupling
- Support for future extension

The `SerialReader` and `Renderer` access the FrameBuffer only through its public methods.

Internal FrameBuffer properties remain private.

---

## 15. Public Interface Concept

The FrameBuffer interface should express protocol operations rather than generic getters and setters.

Producer-side operations:

```text
acquire current write buffer
publish completed frame
```

Consumer-side operations:

```text
check for latest frame
wait for newer frame
copy published frame
```

Methods that expose internal implementation details should be avoided.

Examples of internal details that shall remain private:

- Buffer indices
- Internal NumPy array list
- Sequence counter
- Lock or condition variable
- Published-buffer state

---

## 16. Application Startup Sequence

1. Load shared configuration.
2. Create the `FrameBuffer`.
3. Create the `SerialReader` and pass the FrameBuffer reference.
4. Create the `Renderer` and pass the same FrameBuffer reference.
5. Create a `QThread`.
6. Move the `SerialReader` QObject to the QThread.
7. Connect thread-start, thread-stop and status signals.
8. Connect the `frame_published` signal to the GUI-side frame handling.
9. Start the QThread.
10. Start the Qt event loop.

---

## 17. Application Shutdown Sequence

1. Request interruption of the serial worker.
2. Ensure serial reads use a finite timeout.
3. Allow the SerialReader loop to observe the stop request.
4. Close the serial port.
5. Emit the worker-finished signal.
6. Quit the QThread.
7. Wait for the QThread to terminate.
8. Close the GUI application.

The GUI thread shall not be blocked indefinitely while waiting for serial input.

---

## 18. Testing Strategy

### 18.1 SerialReader Testing

The SerialReader can be tested independently using:

- Recorded byte streams
- Simulated serial ports
- Known packet sequences
- Invalid packet sequences
- Frame-boundary tests

### 18.2 FrameBuffer Testing

The FrameBuffer can be tested independently for:

- Buffer switching
- Correct sequence numbering
- Duplicate-frame prevention
- Private-copy behavior
- Producer–consumer synchronization
- Skipped-frame detection

### 18.3 Renderer Testing

The Renderer can be tested independently using:

- Simulated frames
- Artificial sequence numbers
- Static NumPy arrays
- Simulated frame publication

Thread integration should be added only after the individual components work correctly.

---

## 19. Design Constraints and Decisions

The architecture adopts the following decisions:

- PySide6 is used for the GUI.
- `QThread` is used for serial reception.
- The renderer remains in the GUI thread.
- The FrameBuffer is a passive shared object.
- The FrameBuffer uses two NumPy arrays.
- Each frame contains four-channel `int16` data only.
- Sample interval and frame size are shared configuration constants.
- The consumer copies each published frame immediately.
- A frame sequence counter is used for logical handshaking.
- A condition variable is used for synchronization.
- Qt signals are used for GUI-safe notification.
- No global mutable runtime state is used.
- Serial communication and rendering remain fully decoupled.

---

## 20. Summary

The resulting architecture is a double-buffered producer–consumer system.

The `SerialReader` runs in a dedicated `QThread` and produces complete measurement frames.

The `FrameBuffer` provides the only shared data interface and manages ownership, publication, synchronization and sequence tracking.

The `Renderer` runs in the Qt GUI thread, copies newly published frames and updates the display.

This design allows the serial receiver and rendering engine to be developed, tested and maintained independently while preserving deterministic and thread-safe communication.
