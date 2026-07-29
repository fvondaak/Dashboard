# Handshaking Specification

## Purpose

This document defines the interface between the **Serial Receiver
(Producer)** and the **UI / Rendering Engine (Consumer)**. The objective
is to decouple both development tasks so they can be implemented
independently while exchanging data through a single shared object.

The system consists of:

-   **Producer thread** -- Receives serial packets, assembles complete
    frames and publishes them.
-   **Consumer thread** -- Waits for newly published frames, copies them
    into a private buffer and renders the data.
-   **Shared Frame Buffer** -- Owns the two frame buffers and implements
    the synchronization and handshaking.

------------------------------------------------------------------------

# Overall Architecture

``` text
                Serial Receiver Thread
                     (Producer)
                          │
                  Fill write buffer
                          │
                  Publish complete frame
                          │
                          ▼
              Shared Double Frame Buffer
          (Synchronization + Handshaking)
                          │
              Wait for published frame
                          │
               Copy complete frame
                          │
                          ▼
                UI / Rendering Thread
                    (Consumer)
```

## Producer--Consumer Scheme

### Producer Responsibilities

-   Receive serial packets.
-   Detect complete frames.
-   Decode packet contents.
-   Write decoded samples into the current write buffer.
-   Publish completed frames.
-   Never modify a buffer after publication.

### Consumer Responsibilities

-   Wait for newly published frames.
-   Copy the published frame into a private buffer.
-   Render only from the private copy.
-   Never modify the shared buffers.

# Shared Object Definition

``` python
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
        Synchronization object used to notify consumers when a
        newer frame becomes available.
    """
```

# Shared Object Parameters

## `_buffers`

Two frame buffers with shape

``` text
(SAMPLES_PER_FRAME, CHANNEL_COUNT)
```

Each element is an `int16` measurement value.

Rows represent samples, columns represent channels.

No timestamps are stored.

## `_write_index`

Identifies the buffer currently owned by the producer.

Only the producer may modify this buffer.

## `_published_index`

Identifies the newest completed frame.

The consumer copies this buffer after notification.

## `_frame_sequence`

Monotonically increasing frame counter.

Used to detect new frames and skipped frames.

## `_condition`

Synchronization object used to wake waiting consumers whenever a new
frame has been published.

# Producer Sequence

1.  Acquire the current write buffer.
2.  Receive and decode serial packets.
3.  Store decoded channel values.
4.  Fill the complete frame.
5.  Publish the completed frame.
6.  Switch automatically to the other write buffer.
7.  Continue receiving.

# Consumer Sequence

1.  Wait for a newer published frame.
2.  Verify the sequence number.
3.  Copy the published frame into a private buffer.
4.  Release synchronization.
5.  Render only from the private copy.
6.  Wait for the next frame.

# Ownership Rules

## Producer

-   Owns the current write buffer.
-   Never modifies a published buffer.

## Consumer

-   Never modifies shared buffers.
-   Copies the frame immediately after publication.
-   Renders only from the private copy.

# Buffer Switching

``` text
Frame 0 -> Write Buffer 0 -> Publish -> Switch to Buffer 1
Frame 1 -> Write Buffer 1 -> Publish -> Switch to Buffer 0
Frame 2 -> Write Buffer 0 -> Publish -> ...
```

With a frame duration of approximately two seconds, the consumer has
ample time to copy the published frame before the producer reuses that
buffer.

# Design Goals

-   Clear separation between acquisition and rendering.
-   Thread-safe synchronization.
-   Minimal shared state.
-   Independent implementation and testing.
