# X-Plane manual acceptance checklist

Run this checklist on Windows x64 and macOS Intel/Apple Silicon with X-Plane 12.04 or newer.

- Install the complete plugin directory and confirm a clean load in `Log.txt`.
- Toggle the DCDU from the Plugins menu and the `hoppiedatalinkxp/toggle_window` command.
- Move, resize and pop out the window; restart X-Plane and verify saved geometry.
- At 100%, 150% and 200% UI/DPI scaling, verify that content fills the native client area and never renders outside its frame. Click each page tab, text field and the corners of buttons; hover highlights, click/drag and wheel scrolling must follow the visible controls.
- Repeat after resizing/moving the window, after changing simulator UI scale, on a negative-origin secondary monitor, and in a popped-out OS window. Pop out and restore using only the native title-bar control; confirm no custom POP OUT button is present in either mode, and repeat native pop-out/restore several times without a crash or content displacement.
- Open an unread message and verify its highlighting clears without an ImGui style-stack error.
- Switch Chinese/English and confirm all pages render with the bundled font.
- On a fresh configuration, confirm English is the default. A previously saved Chinese choice must still load as Chinese. Check Station replaces ATSU on STATUS and REQUEST.
- In SETTINGS, confirm DEFAULT ATIS shows VATSIM, IVAO and PilotEdge. Save each choice, restart, and verify the corresponding ATIS product is selected in WX / TELEX and uses the correct service.
- Enter a valid Hoppie code and test CONNECT ping. Disconnect the network and confirm a 15-second timeout does not freeze frames.
- Send a real information request and verify the first poll and subsequent successful polls are scheduled 30 seconds apart (plus HTTP time), with no separate 20-second fast poll. Additional sends must not postpone the existing poll. Network errors retain backoff; recovery returns to 30 seconds.
- Verify a green send-success banner on the initiating page, including immediate METAR/DATA replies. TX must not appear in MESSAGES and there must be no way to reply to a sent REQUEST LOGON.
- Check STATUS shows next poll and last successful poll; verify no `no to address` error after LOGON. On Hoppie's website, verify ATC replies acquire a relayed time when picked up. Do not run a second client polling the same callsign during this test.
- Receive `/data2/20/1/NE/LOGON ACCEPTED` from the pending station; verify current Station is set and Pending Station clears, with the notification visible in MESSAGES. Check CURRENT ATC UNIT announcements, NE LOGON REJECTED, handover acceptance and NE LOGOFF too; unrelated stations must not change the active station.
- Receive `/data2/22/3/NE/MESSAGE NOT SUPPORTED BY THIS ATS UNIT` and verify exact body plus ID 22, REPLY TO 3, RESPONSE NE. It must be unread/alerted and have no reply buttons.
- Check an unknown response kind, uppercase /DATA2/, malformed /data2/, and an old /data1/ payload are all displayed, with unsupported/raw entries read-only. A normal WU instruction in the same batch must remain readable/replyable. Resend a reused ID with different body and confirm it is not lost; only identical retransmissions are suppressed.
- After upgrading from a version that dropped a relayed packet, request a controller resend and reconnect/log on; do not expect normal polling to retrieve previously consumed packets automatically.
- Receive both a TELEX DCL and a CPDLC clearance. Right-click to delete one received row; verify other selections/reply targets remain correct. CLEAR ALL must remove all inbox rows and unread alerts without disconnecting or cancelling a pending LOGON; send a new inbound message afterwards.
- Receive a CPDLC WU departure clearance formatted `CLD ... PDC ... CLRD TO @ZLXY@ OFF @36L@ VIA @OLT8X@ SQUAWK @1116@`. Confirm only the marked fields are orange, all `@` characters are hidden, and wrapping remains aligned at different window sizes and UI scales. Test marked text across an explicit newline and a missing closing marker without losing text.
- DCL must show exactly one ACCEPT button. Click it once: while the send is pending there must be no clickable reply, then green static ACCEPTED after Hoppie returns success. The outgoing reply must be `/data2/<new ID>/<original ID>/N/WILCO` for WU. An unrelated CLIMB/CONTACT instruction must retain its normal reply buttons.
- Reply to ordinary CPDLC instructions: WILCO/AFFIRM/ROGER must appear as green static text below the body; UNABLE/NEGATIVE red; STANDBY orange. Check each reply remains attached to the correct message when changing pages/selections. No TX row may appear, and messages without a crew reply must not show a fabricated result.
- After successful STANDBY, final reply options must remain available. While the final reply is pending, retain orange STANDBY and show SENDING REPLY; on success replace STANDBY with the final reply text/color and remove reply buttons. If the final send fails, keep the last successful STANDBY and show failure feedback, never a false successful final reply. Check server records before explicitly retrying.
- Fail a DCL acceptance with a network timeout; it must not display ACCEPTED or automatically resend. After checking server records, explicitly retry and verify the resulting static ACCEPTED state survives switching away and back to the message. N messages and plain TELEX without CPDLC reply fields remain read-only.
- Disconnect networking during a reply: verify red failure/unconfirmed feedback, no automatic resend and no stuck reply-in-flight state. Check the server log before explicitly retrying. Restore networking and verify polls resume without manually reconnecting.
- Delete/clear an inbox row while its reply is in flight, and disable or change callsign with a poll in flight. Verify no crash, stale result, resurrected row or permanently stalled polling after reconnect.
- Keep the connection running for at least 20 minutes after the first real request; successful poll timestamps should keep advancing. Distinguish local disconnected state from server online expiry during network outages.
- With test callsigns, complete CPDLC LOGON, request/reply, STANDBY, HANDOVER and LOGOFF.
- Send DCL and Oceanic Clearance requests; verify replies remain review-only and do not alter the FMS.
- Verify DCL CALLSIGN cannot be edited or pasted over and equals the connected Hoppie callsign. Edit the unsubmitted STATUS callsign draft or import a different flight plan; DCL must keep the session callsign. Change callsign and reconnect; DCL must now use the new identity in both sender and body.
- Request METAR, TAF, SHORT TAF, VATSIM ATIS, IVAO ATIS and PilotEdge ATIS.
- Fetch online VATSIM, a VATSIM prefile and a SimBrief OFP; verify manual fields win conflicts.
- Enable ADS-C, accept `REQUEST PERIODIC 60`, verify reports, then test CANCEL, plugin disable and callsign change.
- Receive a message and listen for three 350 ms beeps with 150 ms gaps (about 1.35 seconds total), then silence even if the message remains unread. Verify closely spaced arrivals do not overlap playback, and muting SOUND or disabling/unloading the plugin during playback stops it cleanly. Received-message amber flashing stops after reading.
- Inspect preferences and logs to confirm neither Hoppie code nor message bodies are persisted.
