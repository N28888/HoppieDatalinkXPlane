# Architecture and protocol notes

The plugin has three boundaries:

1. `hoppie_core` contains deterministic formatting, nested-brace parsing, CPDLC correlation, session transitions, poll timing, ADS-C contracts and source precedence. It has no XPLM or OS dependency.
2. `NetworkWorker` owns one blocking worker thread. Its queue carries plain strings and identifiers only. Windows uses WinHTTP; macOS uses NSURLSession. Requests reject non-HTTPS URLs and cap timeout and response size.
3. `plugin.cpp` and `DcduWindow` run on X-Plane's main thread. They alone touch datarefs, window/menu/command APIs, OpenGL and `XPLMPlayPCMOnBus`.

## Window coordinates (0.1.1)

XPLM modern-window geometry and mouse/cursor/wheel callbacks all use global desktop
boxels. ImGui layout uses window-local boxels with its origin at the content area's
top left: `(x - left, top - y)`. It does not use the global desktop as its framebuffer.

The GL2 font backend is retained. The draw-list adapter in `ui.cpp` uses the current
callback's `modelview_matrix`, `projection_matrix` and `viewport` datarefs to render
local coordinates through X-Plane's transform. Scissors are projected to framebuffer
pixels, bounded to the client area and viewport, and intersected with any host scissor.
It does not replace the viewport with a boxel-sized pixel viewport. Matrix, texture,
client-array and other modified GL state are restored after drawing. Since 0.1.6,
pop-out/restore is handled exclusively by the native title-bar controls. The DCDU
has no custom pop-out button, pending toggle flag or window-positioning-mode call.

The SDK's [window-coordinate contract](https://developer.x-plane.com/sdk/XPLMDisplay/)
and [drawing-state guidance](https://developer.x-plane.com/article/plugin-guidance-for-opengl-drawing/)
describe these coordinate spaces. `ui-geometry` is a simulator-free CTest regression
for DPI, negative desktop origins, viewport offsets, pop-out transforms and clipping.

## Hoppie lifecycle

- CONNECT posts a `ping`. It does not activate polling.
- A CPDLC, DCL, information request, TELEX or ADS-C report activates polling.
- Normal polls use a fixed 30-second interval, including the first poll after a real request (0.1.5 user requirement).
- Further sends do not postpone a scheduled poll or bypass failure backoff. There is no separate fast-poll timer.
- A failed non-idempotent send is marked failed and is never automatically retried.
- Poll failures back off from 60 seconds up to five minutes.
- Changing callsign clears CPDLC/ADS-C state and requires reconnect.

The selected 30-second interval is faster than the 45–75 seconds recommended by the
[Hoppie API](https://www.hoppie.nl/acars/system/tech.html); it is a user-requested policy, not a server recommendation.

All server-directed requests, including `poll`, carry `to=SERVER`. Hoppie requires a
non-empty destination even when it is ignored for routing. The old empty `to` on
polls prevented mailbox retrieval. See the [Hoppie API](https://www.hoppie.nl/acars/system/tech.html).
Polling is not a persistent socket; transient errors do not force a local disconnect.
The status page exposes poll-in-flight, next poll and last successful poll times.
An unavailable network can still cause the server's online/callsign lock to expire.

The main-thread tick supplies the same steady time used by sends and ADS-C. The
application test links the real worker queue to a scripted, test-only transport and
credential stub: it does not read OS credentials or contact Hoppie. It verifies the
actual POST form, first-request gating, LOGON/DCL/CPDLC inbox delivery, sustained
polling, error recovery and call-sign changes with work in flight.

`parseHoppieResponse` accepts zero or more `{sender type {packet}}` envelopes after `ok`, tracks nested braces in packet bodies, and rejects trailing or unbalanced input. `/data2` is split only at its first four separators so the message body may contain `/`.

The parser also accepts `{numeric-server-id sender type {packet}}`, as encountered
by other Hoppie clients ([Magknight response notes](https://docs.magknight.org/hoppie/)).
In this longer form, numeric IDs are envelope metadata, not sender callsigns.

## CPDLC receive compatibility (0.1.5)

`parseCpdlcPacket()` validates structural fields and numeric message/reply IDs but
does not reject an unknown response kind. Both /data2/ and /DATA2/ are accepted;
body bytes, including slash-separated text, are preserved. N and NE are known
no-reply kinds; unknown kinds are displayed read-only and cannot drive session
transitions. Packets failing structural validation are still added to the inbox
as raw, read-only messages, with no fabricated reply association.

LOGON ACCEPTED with NE now reaches the existing session transition. A matching
CURRENT ATC UNIT announcement can complete a pending logon only if both its sender
and announced station match the pending station; presentation @/_ separators are
normalized only for control-message interpretation. Notifications remain visible.

Retransmission identity is the pair of sender and complete wire packet, not sender
and message ID alone. A reused ID with different body, kind or reply target is
shown. Complete envelopes preceding a malformed outer envelope are retained even
though the poll still reports a parsing error and backs off. A broken outer
envelope that cannot identify sender/type/body cannot be recovered as a CPDLC
message. Previously relayed packets dropped by older versions cannot be restored
from normal polling; a controller resend is needed. TX remains separate/hidden.

## Inbox and send feedback (0.1.2)

`Application::messages()` is the RX inbox. Outgoing protocol state is kept separately
in session memory so hiding TX or clearing the inbox cannot invalidate correlation.
The UI selects a stable local ID, not a vector index, across deletion. Right-click
delete and clear-all remove inbox rows without changing connection, pending ATSU,
outgoing requests or the CPDLC retransmission set; new messages still arrive normally.

`availableReplies()` is shared by the UI and application boundary. Only incoming
CPDLC messages with a response-required kind, no final reply and no in-flight reply
can be answered. An `N` clearance does not gain reply buttons from its text alone.
The in-flight flag is cleared on send result; only HTTP success plus a valid Hoppie
`ok` marks a final response complete. Failed/unknown sends require deliberate manual
retry. Deletion while sending is safe because completion looks up the stable ID.

Since 0.1.6, that same successful completion stores the UI `ReplyAction` in the
original message's `sentReply`. The message detail shows it as static colored text:
positive replies green, negative replies red, STANDBY orange, and DCL ACCEPT as
ACCEPTED (regardless of its WILCO/AFFIRMATIVE/ROGER wire mapping). A successful final
reply replaces STANDBY; pending/failed sends never replace the last confirmed reply.
This is a Hoppie send acknowledgement, not an ATC read receipt. No new persistence,
transport path or dependency is introduced.

Send feedback is a separate FIFO shown on every page, including sends with immediate
DATA responses. Success means acceptance by Hoppie, not delivery to or clearance from
ATC. Polls/ping do not create TX inbox rows or send toasts. Changing callsign or
disabling/disconnecting discards queued requests and ignores stale results; already
in-flight OS requests cannot be recalled and have an unknown outcome locally.

## Message presentation and DCL acceptance (0.1.4)

`formatMessageText()` creates a display-only copy without `@` and marks bytes inside
matched pairs. Raw message bodies stay unchanged for protocol processing. The small
ImGui renderer wraps the complete plain text with the existing font API before
splitting each line into colored runs, preserving word boundaries and UTF-8 glyphs.
The simulator-free `message-ui` test compares its actual draw-list glyphs, UVs and
layout against native ImGui wrapping using the bundled font at multiple widths/scales.

DCL classification requires a departure-clearance indicator (such as CLD/PDC) and
CLR/CLRD/CLEARED TO, rather than any occurrence of CLEARANCE. Reply eligibility is
still shared by UI and application. Only ACCEPT is available for these CPDLC
clearances; N and non-CPDLC envelopes do not acquire reply controls. Its positive
wire response follows the original response kind, consistent with
[EasyCPDLC's WU/AN/R acceptance mapping](https://github.com/quassbutreally/EasyCPDLC/blob/master/EasyCPDLC/MainForm.cs).
The existing send completion and duplicate guards drive the static ACCEPTED label;
failure keeps the message unaccepted and requires deliberate retry.

## Sensitive data

The logon code is never serialized to settings. It exists in process memory and in the OS credential store when available. Network tasks necessarily hold it while constructing a request, but no diagnostic path prints request bodies. Datalink message bodies are intentionally session-only.

## ADS-C boundary

Only `REQUEST PERIODIC n` and `CANCEL` are implemented. `n < 60`, malformed periods and event contracts are rejected. Reports use X-Plane latitude, longitude and MSL altitude at send time and format altitude in hundreds of feet. Disabling ADS-C, disabling the plugin or changing callsign cancels the contract.
