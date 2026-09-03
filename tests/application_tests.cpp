#include "hoppie/application.hpp"
#include "hoppie/credentials.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace hoppie;
using namespace std::chrono_literals;

namespace {
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "line " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

struct Response {
    NetworkPurpose purpose;
    std::string body;
    bool transportOk{true};
    int httpStatus{200};
    bool hold{false};
};

std::mutex serverMutex;
std::condition_variable serverWake;
std::deque<Response> responses;
std::vector<NetworkTask> requests;
bool releaseHeld = false;
int protocolErrors = 0;

void script(NetworkPurpose purpose, std::string body = "ok", bool transportOk = true,
            int httpStatus = 200, bool hold = false) {
    std::lock_guard<std::mutex> lock(serverMutex);
    responses.push_back({purpose, std::move(body), transportOk, httpStatus, hold});
}

std::vector<NetworkTask> observed() {
    std::lock_guard<std::mutex> lock(serverMutex);
    return requests;
}

void release() {
    { std::lock_guard<std::mutex> lock(serverMutex); releaseHeld = true; }
    serverWake.notify_all();
}

struct Flight {
    Application app{""};
    Application::SteadyTime now{};
    Flight() {
        std::lock_guard<std::mutex> lock(serverMutex);
        responses.clear(); requests.clear(); releaseHeld = false;
        app.tick({}, now, {});
        app.setCallsign("CCA3721");
        app.setLogonCode("TEST_ONLY_NOT_A_REAL_CODE", false);
    }
    ~Flight() { release(); }
    void tick() { app.tick({}, now, {}); }
    void advance(std::chrono::seconds duration) { now += duration; tick(); }
    template<class Predicate> void until(Predicate done) {
        for (int i = 0; i < 2000; ++i) {
            tick();
            if (done()) return;
            std::this_thread::sleep_for(1ms);
        }
        throw std::runtime_error("timed out waiting for mock exchange");
    }
    void connect() {
        script(NetworkPurpose::Ping);
        CHECK(app.connect());
        until([&] { return app.connected(); });
    }
    SendFeedback notice() {
        std::optional<SendFeedback> feedback;
        until([&] { feedback = app.takeSendFeedback(); return feedback.has_value(); });
        return *feedback;
    }
    void poll(std::string body = "ok", bool transportOk = true, int httpStatus = 200) {
        const auto due = app.nextPoll();
        if (!due) throw std::runtime_error("poll not scheduled");
        script(NetworkPurpose::Poll, std::move(body), transportOk, httpStatus);
        now = *due;
        tick();
        until([&] { return !app.pollInFlight(); });
    }
};

void testLogonDclAndCpdlcInbox() {
    Flight flight;
    auto& app = flight.app;
    flight.connect();
    CHECK(app.messages().empty());
    CHECK(!app.takeSendFeedback());
    flight.advance(1h);
    CHECK(observed().size() == 1);  // ping alone must not start polling

    script(NetworkPurpose::Send);
    CHECK(app.logon("ZGGG"));
    CHECK(!app.logon("ZGGG"));  // no duplicate while pending
    CHECK(app.messages().empty());
    CHECK(!app.reply(0, ReplyAction::Roger));
    const auto sent = flight.notice();
    CHECK(sent.success && sent.recipient == "ZGGG");
    CHECK(app.messages().empty());
    CHECK(observed().back().body.find("&to=ZGGG&type=cpdlc&packet=%2Fdata2%2F1%2F%2FY%2FREQUEST+LOGON") != std::string::npos);
    flight.advance(29s);
    CHECK(observed().size() == 2);
    CHECK(app.nextPoll() == flight.now + 1s);

    // Clear while LOGON is pending: hidden outbound association must survive.
    app.clearMessages();
    flight.poll("ok {829 ZGGG cpdlc {/data2/10/1/NE/@LOGON ACCEPTED@}} "
                "{ZGHA telex {PREDEP CLEARANCE CCA3721\nCLEARED TO ZLXY {VIA ROUTE}}} "
                "{ZGGG cpdlc {/data2/11//WU/CLIMB TO FL350}}");
    CHECK(observed().back().body.find("&to=SERVER&type=poll&packet=") != std::string::npos);
    CHECK(app.connected());
    CHECK(app.session().currentAtsu() == "ZGGG");
    CHECK(app.session().pendingAtsu().empty());
    CHECK(app.lastSuccessfulPoll() == flight.now);
    CHECK(app.messages().size() == 3);
    CHECK(std::all_of(app.messages().begin(), app.messages().end(), [](const auto& m) {
        return m.direction == MessageDirection::Received;
    }));
    CHECK(app.messages()[1].peer == "ZGHA");
    CHECK(app.messages()[1].body.find("{VIA ROUTE}") != std::string::npos);
    CHECK(availableReplies(app.messages()[0]).empty());
    CHECK(!app.reply(0, ReplyAction::Roger));
    CHECK(availableReplies(app.messages()[2]).size() == 3);

    // Reply acknowledgement, duplicate prevention, no TX rows in the inbox.
    script(NetworkPurpose::Send);
    CHECK(app.reply(2, ReplyAction::Wilco));
    CHECK(app.messages()[2].replyInFlight);
    CHECK(!app.reply(2, ReplyAction::Unable));
    CHECK(flight.notice().success);
    CHECK(app.messages()[2].finalReplySent);
    CHECK(!app.messages()[2].replyInFlight);
    CHECK(!app.reply(2, ReplyAction::Unable));
    CHECK(app.messages().size() == 3);
    CHECK(observed().back().body.find("%2F11%2FN%2FWILCO") != std::string::npos);

    const auto id = app.messages()[1].localId;
    CHECK(app.deleteMessage(id));
    CHECK(!app.deleteMessage(id));
    CHECK(app.messages().size() == 2);
    app.clearMessages();
    CHECK(app.messages().empty() && app.unreadCount() == 0);
    CHECK(!app.consumeSoundPending());
    CHECK(app.connected() && app.session().currentAtsu() == "ZGGG");
    // CPDLC duplicates remain suppressed even after their visible rows are deleted.
    flight.poll("ok {ZGGG cpdlc {/data2/11//WU/CLIMB TO FL350}} "
                "{ZGGG cpdlc {/data2/12//R/REPORT LEVEL FL350}}");
    CHECK(app.messages().size() == 1);
    CHECK(app.messages()[0].cpdlc->messageId == "12");
    CHECK(app.unreadCount() == 1);
}

void testAllCpdlcReceivePaths() {
    Flight flight;
    auto& app = flight.app;
    flight.connect();
    script(NetworkPurpose::Send);
    CHECK(app.logon("ZGZU"));
    CHECK(flight.notice().success);
    CHECK(app.nextPoll() == flight.now + 30s);
    flight.poll("ok {ZGZU cpdlc {/data2/20/1/NE/LOGON ACCEPTED}} "
                "{ZGZU cpdlc {/data2/21//NE/CURRENT ATC UNIT@_@ZGZU@_@GUANGZHOU CTL}} "
                "{ZGZU cpdlc {/data2/22/3/NE/MESSAGE NOT SUPPORTED BY THIS ATS UNIT}} "
                "{OTHER CPDLC {/DATA2/23//XX/UNKNOWN KIND / BODY}} "
                "{OTHER cpdlc {/data1/24//WU/LEGACY PACKET}} "
                "{OTHER cpdlc {/data2/25/NE/MISSING HEADER FIELD}} "
                "{OTHER cpdlc {UNFRAMED @MESSAGE@}} "
                "{OTHER cpdlc {/data2/26///EMPTY RESPONSE TYPE}}");
    CHECK(app.session().currentAtsu() == "ZGZU" && app.session().pendingAtsu().empty());
    CHECK(app.messages().size() == 8 && app.unreadCount() == 8);
    CHECK(app.consumeSoundPending());
    CHECK(app.messages()[2].body == "MESSAGE NOT SUPPORTED BY THIS ATS UNIT");
    CHECK(app.messages()[2].cpdlc->responseKind == "NE" && app.messages()[2].cpdlc->replyTo == "3");
    CHECK(app.messages()[3].body == "UNKNOWN KIND / BODY");
    CHECK(app.messages()[4].body == "/data1/24//WU/LEGACY PACKET" && !app.messages()[4].cpdlc);
    CHECK(app.messages()[5].body == "/data2/25/NE/MISSING HEADER FIELD" && !app.messages()[5].cpdlc);
    CHECK(app.messages()[6].body == "UNFRAMED @MESSAGE@" && !app.messages()[6].cpdlc);
    for (std::size_t i = 0; i < app.messages().size(); ++i) {
        CHECK(app.messages()[i].direction == MessageDirection::Received);
        CHECK(availableReplies(app.messages()[i]).empty());
        CHECK(!app.reply(i, ReplyAction::Roger));
    }
    CHECK(app.nextPoll() == flight.now + 30s);
    // Reused ID with different content is a new message, unlike an identical retransmission.
    flight.poll("ok {ZGZU cpdlc {/data2/22/3/NE/MESSAGE NOT SUPPORTED BY THIS ATS UNIT}} "
                "{ZGZU cpdlc {/data2/22//WU/CLIMB TO @FL350@}} "
                "{ZGZU cpdlc {/data2/23//XX/LOGOFF}} "
                "{OTHER cpdlc {/data2/22//NE/DIFFERENT SENDER}}");
    CHECK(app.messages().size() == 11);
    CHECK(app.session().currentAtsu() == "ZGZU");
    CHECK(availableReplies(app.messages()[8]).size() == 3);
    CHECK(availableReplies(app.messages()[9]).empty());
    // A malformed outer envelope at the end must not hide complete messages before it.
    flight.poll("ok {ZGZU cpdlc {/data2/30//NE/COMPLETE ENVELOPE SURVIVES}} {BROKEN");
    CHECK(app.messages().size() == 12);
    CHECK(app.messages().back().body == "COMPLETE ENVELOPE SURVIVES");
    CHECK(app.nextPoll() == flight.now + 60s);
    flight.poll("ok {ZGZU cpdlc {/data2/31//NE/LOGOFF}}");
    CHECK(app.session().currentAtsu().empty() && app.messages().size() == 13);
    CHECK(app.connected() && app.nextPoll() == flight.now + 30s);
}

void testDclStartsPollingWithoutCpdlcLogon() {
    Flight flight;
    auto& app = flight.app;
    flight.connect();
    DclRequest dcl{"ZGHA", "CCA3721", "A20N", "ZGHA", "ZLXY", "205", "B", ""};
    script(NetworkPurpose::Send);
    CHECK(app.sendDcl(dcl));
    CHECK(flight.notice().success);
    CHECK(app.messages().empty());
    flight.poll("ok {ZGHA telex {CCA3721 CLEARED TO ZLXY\nSQUAWK 1234}}");
    CHECK(app.messages().size() == 1 && app.messages()[0].type == "telex");
    CHECK(app.session().currentAtsu().empty());
    // A stale/imported draft must not control either the sender or DCL body callsign.
    dcl.callsign = "DIFFERENT";
    script(NetworkPurpose::Send);
    CHECK(app.sendDcl(dcl));
    CHECK(flight.notice().success);
    CHECK(dcl.callsign == "DIFFERENT"); // caller's draft is not modified by the send boundary
    CHECK(observed().back().body.find("&from=CCA3721&to=ZGHA&type=telex&packet="
          "REQUEST+PREDEP+CLEARANCE+CCA3721+A20N+TO+ZLXY+AT+ZGHA+STAND+205+ATIS+B") != std::string::npos);
    app.setCallsign("CCA9999");
    const auto count = observed().size();
    CHECK(!app.sendDcl(dcl));
    CHECK(observed().size() == count);
    flight.connect();
    script(NetworkPurpose::Send);
    CHECK(app.sendDcl(dcl));
    CHECK(flight.notice().success);
    CHECK(observed().back().body.find("&from=CCA9999&to=ZGHA&type=telex&packet="
          "REQUEST+PREDEP+CLEARANCE+CCA9999+A20N+TO+ZLXY+AT+ZGHA+STAND+205+ATIS+B") != std::string::npos);
}

void testDclAcceptLifecycle() {
    Flight flight;
    auto& app = flight.app;
    flight.connect();
    const std::string body = "CLD 2035 260903 ZGHA PDC 002 CLRD TO @ZLXY@ OFF @36L@ "
                             "VIA @OLT8X@ SQUAWK @1116@ NEXT FREQ @132.750@";
    script(NetworkPurpose::Send, "ok {ZGZU cpdlc {/data2/42//WU/" + body + "}}");
    CHECK(app.sendDcl({"ZGZU", "", "A20N", "ZGHA", "ZLXY", "205", "B", ""}));
    CHECK(flight.notice().success);
    CHECK(app.messages().size() == 1);
    CHECK(app.messages()[0].body == body);
    CHECK(availableReplies(app.messages()[0]) == std::vector<ReplyAction>({ReplyAction::Accept}));
    CHECK(!app.reply(0, ReplyAction::Wilco));
    CHECK(!app.reply(0, ReplyAction::Reject));
    CHECK(!app.reply(0, ReplyAction::Standby));

    script(NetworkPurpose::Send, "", false, 0);
    CHECK(app.reply(0, ReplyAction::Accept));
    CHECK(app.messages()[0].replyInFlight && !app.messages()[0].finalReplySent);
    CHECK(!app.reply(0, ReplyAction::Accept));
    CHECK(!flight.notice().success);
    CHECK(!app.messages()[0].replyInFlight && !app.messages()[0].finalReplySent);
    CHECK(availableReplies(app.messages()[0]) == std::vector<ReplyAction>({ReplyAction::Accept}));
    CHECK(!app.messages()[0].sentReply);

    script(NetworkPurpose::Send);
    CHECK(app.reply(0, ReplyAction::Accept)); // explicit retry after checking the failed send
    CHECK(flight.notice().success);
    CHECK(app.messages()[0].finalReplySent && app.messages()[0].state == MessageState::Complete);
    CHECK(app.messages()[0].sentReply == ReplyAction::Accept); // UI ACCEPTED, wire WILCO
    CHECK(availableReplies(app.messages()[0]).empty());
    CHECK(!app.reply(0, ReplyAction::Accept));
    CHECK(app.messages()[0].body == body);
    CHECK(observed().back().body.find("&to=ZGZU&type=cpdlc&packet=%2Fdata2%2F2%2F42%2FN%2FWILCO") != std::string::npos);
    CHECK(app.messages().size() == 1); // TX remains hidden
}

void testSentReplyLifecycle() {
    Flight flight;
    auto& app = flight.app;
    flight.connect();
    script(NetworkPurpose::Send, "ok {ZGGG cpdlc {/data2/81//WU/CLIMB TO FL350}} "
        "{ZGGG cpdlc {/data2/82//WU/DESCEND TO FL250}} "
        "{ZGGG cpdlc {/data2/83//AN/CONFIRM READY}} "
        "{ZGGG cpdlc {/data2/84//AN/CONFIRM SPEED}} "
        "{ZGGG cpdlc {/data2/85//R/CONTACT CONTROL}}");
    CHECK(app.sendTelex("ZGGG", "TEST"));
    CHECK(flight.notice().success);
    const std::vector<ReplyAction> actions{ReplyAction::Wilco, ReplyAction::Unable, ReplyAction::Affirm,
                                         ReplyAction::Negative, ReplyAction::Roger};
    CHECK(app.messages().size() == actions.size());
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const auto& message = app.messages()[i];
        CHECK(!message.sentReply);
        script(NetworkPurpose::Send);
        CHECK(app.reply(i, ReplyAction::Standby));
        CHECK(!message.sentReply && message.replyInFlight);
        CHECK(flight.notice().success);
        CHECK(message.sentReply == ReplyAction::Standby && !message.finalReplySent);
        CHECK(!availableReplies(message).empty());

        script(NetworkPurpose::Send, "", false, 0);
        CHECK(app.reply(i, actions[i]));
        CHECK(message.sentReply == ReplyAction::Standby && message.replyInFlight);
        CHECK(!flight.notice().success);
        CHECK(message.sentReply == ReplyAction::Standby && !message.finalReplySent);
        CHECK(message.state == MessageState::Failed && !message.replyInFlight);

        script(NetworkPurpose::Send);
        CHECK(app.reply(i, actions[i])); // explicit retry only
        CHECK(message.sentReply == ReplyAction::Standby);
        CHECK(flight.notice().success);
        CHECK(message.sentReply == actions[i] && message.finalReplySent);
        CHECK(availableReplies(message).empty());
        CHECK(!app.reply(i, ReplyAction::Standby));
        CHECK(message.sentReply == actions[i]);
        for (std::size_t j = 0; j < i; ++j) CHECK(app.messages()[j].sentReply == actions[j]);
    }
    CHECK(app.messages().size() == actions.size()); // replies never create TX inbox rows
}

void testSettingsAndMessageSound() {
    Flight flight;
    auto& app = flight.app;
    CHECK(app.settings().language == "en");
    CHECK(app.settings().defaultAtisSource == "vatatis");
    CHECK(app.settings().soundEnabled);
    auto settings = app.settings();
    settings.language = "zh";
    app.updateSettings(settings);
    CHECK(app.settings().language == "zh"); // explicit choices remain supported
    settings.language = "invalid";
    settings.soundEnabled = false;
    app.updateSettings(settings);
    CHECK(app.settings().language == "en");
    flight.connect();
    script(NetworkPurpose::Send, "ok {ACARS data {TEST METAR}}");
    CHECK(app.sendInfo("metar", "ZBAA"));
    CHECK(flight.notice().success);
    CHECK(app.messages().size() == 1 && !app.consumeSoundPending());
    settings.soundEnabled = true;
    app.updateSettings(settings);
    flight.poll("ok {ZGHA telex {TEST CLEARANCE}}");
    CHECK(app.consumeSoundPending());
    CHECK(!app.consumeSoundPending()); // unread messages must not trigger endlessly
}

void testSendFeedbackAndPollRecovery() {
    Flight flight;
    auto& app = flight.app;
    flight.connect();
    script(NetworkPurpose::Send, "ok {ACARS data {ZBAA METAR DATA}}");
    CHECK(app.sendInfo("metar", "ZBAA"));
    CHECK(flight.notice().success); // even an immediate DATA reply must retain success feedback
    CHECK(app.messages().size() == 1 && app.messages()[0].type == "DATA");
    flight.poll("", false, 0);
    CHECK(app.connected());
    CHECK(app.statusText().find("WILL RETRY") != std::string::npos);
    CHECK(app.nextPoll() == flight.now + 60s);
    flight.poll("error {temporary unavailable}");
    CHECK(app.connected());
    CHECK(app.nextPoll() == flight.now + 120s);
    flight.poll("unavailable", true, 503);
    CHECK(app.connected());
    CHECK(app.nextPoll() == flight.now + 240s);
    flight.poll();
    CHECK(app.connected());
    CHECK(app.statusText() == "CONNECTED — POLL OK");
    for (int i = 0; i < 20; ++i) flight.poll();
    CHECK(app.connected());  // no idle disconnect across simulated minutes

    script(NetworkPurpose::Send, "", false, 0);
    CHECK(app.sendTelex("ZGHA", "TEST MESSAGE"));
    CHECK(!flight.notice().success);
    const auto count = observed().size();
    flight.advance(19s);
    CHECK(observed().size() == count); // failed non-idempotent sends never auto-retry
    CHECK(app.messages().size() == 1);

    script(NetworkPurpose::Send, "error {recipient unavailable}");
    CHECK(app.logon("ZGGG"));
    CHECK(!flight.notice().success);
    CHECK(app.session().pendingAtsu().empty());
    CHECK(app.session().state() == CpdlcSessionState::Offline);
    script(NetworkPurpose::Send);
    CHECK(app.logon("ZGGG"));
    CHECK(flight.notice().success);
}

void testReplyFailureAndDeletionWhileSending() {
    Flight flight;
    auto& app = flight.app;
    flight.connect();
    script(NetworkPurpose::Send, "ok {ZGGG cpdlc {/data2/51//WU/CLIMB TO FL350}}");
    CHECK(app.sendTelex("ZGGG", "TEST"));
    CHECK(flight.notice().success);
    script(NetworkPurpose::Send, "", false, 0);
    CHECK(app.reply(0, ReplyAction::Wilco));
    CHECK(!flight.notice().success);
    CHECK(!app.messages()[0].finalReplySent && !app.messages()[0].replyInFlight);
    CHECK(!availableReplies(app.messages()[0]).empty());
    script(NetworkPurpose::Send);
    CHECK(app.reply(0, ReplyAction::Unable)); // explicit retry only
    CHECK(app.deleteMessage(app.messages()[0].localId));
    CHECK(flight.notice().success); // completion must not resurrect deleted source row
    CHECK(app.messages().empty());
}

void testCallsignChangeInFlightAndDisable() {
    Flight flight;
    auto& app = flight.app;
    flight.connect();
    script(NetworkPurpose::Send);
    CHECK(app.sendTelex("ZGHA", "TEST"));
    CHECK(flight.notice().success);
    script(NetworkPurpose::Poll, "ok {OLD cpdlc {/data2/90//N/OLD CALLSIGN MESSAGE}}", true, 200, true);
    flight.now = *app.nextPoll();
    flight.tick();
    flight.until([&] { return observed().size() == 3; });
    CHECK(app.pollInFlight());
    CHECK(app.sendTelex("ZGHA", "QUEUED OLD CALLSIGN"));
    app.setCallsign("CCA9999");
    CHECK(!app.connected() && !app.pollInFlight() && !app.busy());
    CHECK(!app.pollingActivated());
    const auto cancelled = app.takeSendFeedback();
    CHECK(cancelled && !cancelled->success);
    release();
    flight.connect();
    CHECK(app.messages().empty()); // late old-call-sign result is ignored
    script(NetworkPurpose::Send);
    CHECK(app.sendTelex("ZGHA", "TEST NEW CALLSIGN"));
    CHECK(flight.notice().success);
    flight.poll("ok {ZGHA telex {NEW CALLSIGN MESSAGE}}");
    CHECK(app.messages().size() == 1);
    CHECK(observed().back().body.find("&from=CCA9999&") != std::string::npos);
    app.disable();
    const auto count = observed().size();
    flight.advance(1h);
    CHECK(!app.connected() && !app.pollInFlight());
    CHECK(observed().size() == count);
}

void testDisableReleasesPendingReply() {
    Flight flight;
    auto& app = flight.app;
    flight.connect();
    script(NetworkPurpose::Send, "ok {ZGGG cpdlc {/data2/71//WU/CLIMB TO FL350}}");
    CHECK(app.sendTelex("ZGGG", "TEST"));
    CHECK(flight.notice().success);
    script(NetworkPurpose::Send, "ok", true, 200, true);
    CHECK(app.reply(0, ReplyAction::Wilco));
    flight.until([&] { return observed().size() == 3; });
    app.disable();
    CHECK(!app.messages()[0].replyInFlight && !app.messages()[0].finalReplySent);
    CHECK(app.messages()[0].state == MessageState::Failed);
    CHECK(!app.messages()[0].sentReply);
    CHECK(!flight.notice().success);
    release();
}

void testTxReplyGuard() {
    Flight flight;
    flight.connect();
    DatalinkMessage invalidInboxEntry;
    invalidInboxEntry.direction = MessageDirection::Sent;
    invalidInboxEntry.cpdlc = CpdlcPacket{"1", "", "Y", "REQUEST LOGON"};
    invalidInboxEntry.peer = "ZGGG";
    flight.app.messages().push_back(invalidInboxEntry);
    CHECK(!flight.app.reply(0, ReplyAction::Roger));
    CHECK(observed().size() == 1);
}
}  // namespace

namespace hoppie {
// Test-only OS boundary. No credentials or live network are accessed by this executable.
std::optional<std::string> loadLogonCredential() { return std::nullopt; }
bool saveLogonCredential(std::string_view) { return true; }
bool deleteLogonCredential() { return true; }
NetworkResult performPlatformHttps(const NetworkTask& task) {
    std::unique_lock<std::mutex> lock(serverMutex);
    requests.push_back(task);
    if (task.timeoutSeconds != 15 || task.url.rfind("https://", 0) != 0) ++protocolErrors;
    if (responses.empty()) {
        ++protocolErrors;
        return {task.id, task.purpose, true, 200, "error {unexpected request}", {}};
    }
    auto reply = std::move(responses.front()); responses.pop_front();
    if (reply.purpose != task.purpose) ++protocolErrors;
    if (reply.hold) serverWake.wait(lock, [] { return releaseHeld; });
    if (task.purpose == NetworkPurpose::Poll && task.body.find("&to=SERVER&type=poll&") == std::string::npos) {
        ++protocolErrors;
        return {task.id, task.purpose, true, 200, "error {no to address}", {}};
    }
    return {task.id, task.purpose, reply.transportOk, reply.httpStatus, reply.body,
             reply.transportOk ? "" : "timeout"};
}
}  // namespace hoppie

int main() {
    try {
        testLogonDclAndCpdlcInbox();
        testAllCpdlcReceivePaths();
        testDclStartsPollingWithoutCpdlcLogon();
        testDclAcceptLifecycle();
        testSentReplyLifecycle();
        testSettingsAndMessageSound();
        testSendFeedbackAndPollRecovery();
        testReplyFailureAndDeletionWhileSending();
        testCallsignChangeInFlightAndDisable();
        testDisableReleasesPendingReply();
        testTxReplyGuard();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; ++failures;
    }
    CHECK(protocolErrors == 0);
    if (failures) return EXIT_FAILURE;
    std::cout << "Application: mock Hoppie polling, DCL/CPDLC RX, send feedback, deletion and recovery passed\n";
    return EXIT_SUCCESS;
}
