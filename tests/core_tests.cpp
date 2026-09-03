#include "hoppie/core.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "line " << line << ": check failed: " << expression << '\n';
        ++failures;
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

template <typename Function>
void checkThrows(Function function, int line) {
    try {
        function();
        std::cerr << "line " << line << ": expected exception\n";
        ++failures;
    } catch (const std::exception&) {
    }
}

#define CHECK_THROWS(expression) checkThrows([&] { (void)(expression); }, __LINE__)

using namespace hoppie;

void testFormEncoding() {
    CHECK(formEncode("A B+C/{x}") == "A+B%2BC%2F%7Bx%7D");
    HoppieRequest request{"secret", "CCA123", "EDYY", "cpdlc", "/data2/1//Y/HELLO"};
    CHECK(toFormBody(request) ==
          "logon=secret&from=CCA123&to=EDYY&type=cpdlc&packet=%2Fdata2%2F1%2F%2FY%2FHELLO");
    CHECK(toFormBody({"test", "CCA123", "", "poll", ""}) ==
          "logon=test&from=CCA123&to=SERVER&type=poll&packet=");
    CHECK(toFormBody({"test", "CCA123", "", "inforeq", "metar ZBAA"}).find("&to=SERVER&") != std::string::npos);
    CHECK(toFormBody({"test", "CCA123", "", "ping", ""}).find("&to=SERVER&") != std::string::npos);
}

void testEnvelopeParser() {
    auto parsed = parseHoppieResponse(
        "ok {EDYY cpdlc {/data2/8/7/WU/CLIMB TO FL350}} "
        "{SERVER telex {TEXT {WITH} NESTING}}");
    CHECK(parsed.ok);
    CHECK(parsed.envelopes.size() == 2);
    CHECK(parsed.envelopes[0].sender == "EDYY");
    CHECK(parsed.envelopes[0].packet == "/data2/8/7/WU/CLIMB TO FL350");
    CHECK(parsed.envelopes[1].packet == "TEXT {WITH} NESTING");

    parsed = parseHoppieResponse("ok");
    CHECK(parsed.ok && parsed.envelopes.empty());
    CHECK(!parseHoppieResponse("error callsign already in use").ok);
    CHECK(!parseHoppieResponse("ok {EDYY cpdlc {BROKEN}").ok);
    CHECK(!parseHoppieResponse("garbage").ok);
    parsed = parseHoppieResponse("ok {12345 ZGHA telex {PREDEP CLEARANCE\n{ROUTE}}} "
                                 "{ZGGG cpdlc {/data2/2/1/N/LOGON ACCEPTED}}");
    CHECK(parsed.ok && parsed.envelopes.size() == 2);
    CHECK(parsed.envelopes[0].sender == "ZGHA" && parsed.envelopes[0].type == "telex");
    CHECK(parsed.envelopes[1].sender == "ZGGG");
    CHECK(!parseHoppieResponse("ok {NOT_AN_ID ZGHA telex {BROKEN}}").ok);
    CHECK(!parseHoppieResponse("ok {123 ZGHA telex}").ok);
}

void testCpdlcPacketsAndReplies() {
    std::string error;
    auto packet = parseCpdlcPacket("/data2/42/17/WU/CLIMB TO FL350", &error);
    CHECK(packet.has_value());
    CHECK(packet->messageId == "42");
    CHECK(packet->replyTo == "17");
    CHECK(packet->responseKind == "WU");
    CHECK(packet->body == "CLIMB TO FL350");
    const auto unknown = parseCpdlcPacket("/data2/42/17/XX/UNSUPPORTED BUT VISIBLE", &error);
    CHECK(unknown && unknown->responseKind == "XX");
    CHECK(error.empty() && unknown && validReplies(*unknown).empty());
    const auto ne = parseCpdlcPacket("/data2/22/3/NE/MESSAGE NOT SUPPORTED BY THIS ATS UNIT", &error);
    CHECK(ne && ne->messageId == "22" && ne->replyTo == "3" && ne->responseKind == "NE");
    CHECK(ne && ne->body == "MESSAGE NOT SUPPORTED BY THIS ATS UNIT" && validReplies(*ne).empty());
    CHECK(isKnownCpdlcResponseKind("NE") && !isKnownCpdlcResponseKind("XX"));
    const auto upper = parseCpdlcPacket(" \r\n/DATA2/23//ne/Body / with / slashes\n");
    CHECK(upper && upper->responseKind == "NE" && upper->body == "Body / with / slashes\n");
    CHECK(!parseCpdlcPacket("/data2//1/WU/MISSING ID", &error));
    CHECK(!parseCpdlcPacket("/data2/bad id/1/WU/INVALID ID", &error));
    CHECK(!parseCpdlcPacket("/data2/1/BAD/WU/INVALID REPLY ID", &error));
    CHECK(!parseCpdlcPacket("/data2/1/NE/MISSING FIELD", &error));
    CHECK(!parseCpdlcPacket("/data1/42/17/WU/BAD", &error));

    const auto wu = validReplies(*packet);
    CHECK(wu == std::vector<ReplyAction>({ReplyAction::Standby, ReplyAction::Wilco,
                                          ReplyAction::Unable}));
    CHECK(validReplies(CpdlcPacket{"1", "", "AN", "CONFIRM"}).size() == 3);
    CHECK(validReplies(CpdlcPacket{"1", "", "R", "REPORT"}).size() == 3);
    CHECK(validReplies(CpdlcPacket{"1", "", "Y", "DCL"}, true) ==
          std::vector<ReplyAction>({ReplyAction::Accept}));
    CHECK(validReplies(CpdlcPacket{"1", "", "Y", "RESPOND"}) ==
          std::vector<ReplyAction>({ReplyAction::Standby, ReplyAction::Roger,
                                    ReplyAction::Unable}));
    CHECK(validReplies(CpdlcPacket{"1", "", "N", "NOTICE"}).empty());
    CHECK(validReplies(CpdlcPacket{"1", "", "N", "CLEARANCE RECEIVED"}, true).empty());
    CHECK(buildReplyPacket("43", *packet, ReplyAction::Wilco) ==
          "/data2/43/42/N/WILCO");

    DatalinkMessage message;
    CHECK(!message.sentReply);
    CHECK(markFinalReply(message, ReplyAction::Standby));
    CHECK(!message.finalReplySent && message.state == MessageState::Standby);
    CHECK(message.sentReply == ReplyAction::Standby);
    CHECK(markFinalReply(message, ReplyAction::Wilco));
    CHECK(message.finalReplySent && message.state == MessageState::Complete);
    CHECK(message.sentReply == ReplyAction::Wilco);
    CHECK(!markFinalReply(message, ReplyAction::Unable));
    CHECK(message.sentReply == ReplyAction::Wilco);
    DatalinkMessage sent;
    sent.direction = MessageDirection::Sent;
    sent.cpdlc = CpdlcPacket{"1", "", "Y", "REQUEST LOGON"};
    CHECK(availableReplies(sent).empty());
    CHECK(!markFinalReply(sent, ReplyAction::Roger));
    CHECK(!sent.sentReply);
    sent.direction = MessageDirection::Received;
    CHECK(!availableReplies(sent).empty());
    sent.replyInFlight = true;
    CHECK(availableReplies(sent).empty());
}

void testDclPresentationAndReplies() {
    const std::string body = "CLD 2035 260903 ZGHA PDC 002 CLRD TO @ZLXY@ OFF "
        "@36L@ VIA @OLT8X@ SQUAWK @1116@ NEXT FREQ @132.750@ INITIAL ALT @1200 M@ "
        "@QNH 1008@ NEXT FREQ @132.750@ DEP FREQ @132.750@ FINAL ALT @FL 11600 M@ "
        "@READBACK UNNECESSARY@";
    const auto display = formatMessageText(body);
    CHECK(display.text.find('@') == std::string::npos);
    CHECK(display.text.find("SQUAWK 1116 NEXT FREQ 132.750") != std::string::npos);
    CHECK(display.text.size() == display.highlighted.size());
    CHECK(!display.highlighted[0]);
    for (const std::string field : {"ZLXY", "36L", "OLT8X", "1116", "132.750", "1200 M",
                                    "QNH 1008", "FL 11600 M", "READBACK UNNECESSARY"}) {
        const auto offset = display.text.find(field);
        CHECK(offset != std::string::npos);
        if (offset != std::string::npos)
            CHECK(std::all_of(display.highlighted.begin() + offset,
                              display.highlighted.begin() + offset + field.size(), [](bool h) { return h; }));
    }
    CHECK(isDclClearance(body));
    CHECK(isDclClearance("predep clearance CCA3721\ncleared to @ZLXY@"));
    CHECK(isDclClearance("CLD 2035 260903 ZGHA CLRD TO ZLXY"));
    CHECK(!isDclClearance("REQUEST PREDEP CLEARANCE CCA3721 A20N TO ZLXY AT ZGHA"));
    CHECK(!isDclClearance("PREDEP CLEARANCE REJECTED"));
    CHECK(!isDclClearance("CLRC CONFIRMED"));
    CHECK(!isDclClearance("OCEANIC CLEARANCE CLEARED TO DOGAL"));
    CHECK(!isDclClearance("CLIMB TO FL350"));
    DatalinkMessage message;
    message.body = body;
    for (const auto& item : std::vector<std::pair<std::string, std::string>>{
             {"WU", "WILCO"}, {"AN", "AFFIRMATIVE"}, {"R", "ROGER"}, {"Y", "ROGER"}}) {
        message.cpdlc = CpdlcPacket{"42", "", item.first, body};
        CHECK(availableReplies(message) == std::vector<ReplyAction>({ReplyAction::Accept}));
        CHECK(buildReplyPacket("43", *message.cpdlc, ReplyAction::Accept) == "/data2/43/42/N/" + item.second);
    }
    message.cpdlc->responseKind = "N";
    CHECK(availableReplies(message).empty());
    CHECK_THROWS(buildReplyPacket("43", *message.cpdlc, ReplyAction::Accept));
    message.cpdlc->responseKind = "WU";
    message.replyInFlight = true;
    CHECK(availableReplies(message).empty());
    CHECK(markFinalReply(message, ReplyAction::Accept));
    CHECK(message.finalReplySent && message.state == MessageState::Complete);
    CHECK(message.sentReply == ReplyAction::Accept);
    CHECK(availableReplies(message).empty());

    CHECK(formatMessageText("").text.empty());
    CHECK(formatMessageText("@@").text.empty());
    const auto adjacent = formatMessageText("@ONE@@TWO@ END");
    CHECK(adjacent.text == "ONETWO END" && adjacent.highlighted[5] && !adjacent.highlighted[6]);
    const auto unmatched = formatMessageText("@ONE@ @TWO");
    CHECK(unmatched.text == "ONE TWO" && unmatched.highlighted[0] && !unmatched.highlighted[4]);
    const auto multiline = formatMessageText("X @QNH\n1008@\r\nEND");
    CHECK(multiline.text == "X QNH\n1008\r\nEND" && multiline.highlighted[6]);
    CHECK(body.find("@ZLXY@") != std::string::npos); // presentation never mutates the raw message
}

void testSessionState() {
    CpdlcSession session;
    session.beginConnect("cca123");
    CHECK(session.state() == CpdlcSessionState::Connecting);
    session.pingSucceeded();
    CHECK(session.state() == CpdlcSessionState::Offline);
    CHECK(session.beginLogon("edyy"));
    session.onPacket("OTHER", {"9", "1", "N", "LOGON ACCEPTED"});
    CHECK(session.state() == CpdlcSessionState::LogonPending);
    session.onPacket("OTHER", {"10", "1", "N", "UNABLE"});
    CHECK(session.state() == CpdlcSessionState::LogonPending);
    session.onPacket("EDYY", {"2", "1", "Y", "LOGON ACCEPTED"});
    CHECK(session.state() == CpdlcSessionState::Active);
    CHECK(session.currentAtsu() == "EDYY");
    session.onPacket("EDYY", {"90", "", "WU", "CONTACT @WIEN SOUTH CTR@ @133.80@"});
    CHECK(session.state() == CpdlcSessionState::Active);
    CHECK(session.pendingAtsu().empty());
    session.onPacket("OTHER", {"91", "", "N", "LOGOFF"});
    CHECK(session.state() == CpdlcSessionState::Active);

    session.onPacket("EDYY", {"3", "", "N", "HANDOVER EGGX"});
    CHECK(session.state() == CpdlcSessionState::HandoverPending);
    CHECK(session.pendingAtsu() == "EGGX");
    session.onPacket("EGGX", {"4", "3", "Y", "LOGON ACCEPTED"});
    CHECK(session.state() == CpdlcSessionState::Active);
    CHECK(session.currentAtsu() == "EGGX");

    CHECK(session.beginLogoff());
    session.onPacket("EGGX", {"5", "", "N", "LOGOFF"});
    CHECK(session.state() == CpdlcSessionState::Offline);

    session.beginLogon("EDYY");
    session.onPacket("EDYY", {"6", "", "N", "LOGON REJECTED"});
    CHECK(session.state() == CpdlcSessionState::Offline);
    CHECK(session.updateCallsign("CCA456"));
    CHECK(session.callsign() == "CCA456");
    CHECK(session.beginLogon("ZGGG"));
    session.onSendResult("ZGGG", {"100", "", "Y", "REQUEST LOGON"}, false);
    CHECK(session.state() == CpdlcSessionState::Offline && session.pendingAtsu().empty());
    CHECK(session.beginLogon("ZGGG"));
    session.onPacket("ZGGG", {"101", "100", "N", "@LOGON@\n @ACCEPTED@"});
    CHECK(session.state() == CpdlcSessionState::Active);
    CHECK(session.beginLogoff());
    session.onSendResult("ZGGG", {"102", "", "N", "LOGOFF"}, false);
    CHECK(session.state() == CpdlcSessionState::Active);
    CHECK(session.beginLogoff());
    session.onSendResult("ZGGG", {"103", "", "N", "LOGOFF"}, true);
    CHECK(session.state() == CpdlcSessionState::Offline && session.currentAtsu().empty());
}

void testNeSessionNotifications() {
    CpdlcSession session;
    session.beginConnect("CCA3721");
    session.pingSucceeded();
    CHECK(session.beginLogon("ZGZU"));
    session.onPacket("OTHER", {"20", "1", "NE", "LOGON ACCEPTED"});
    CHECK(session.currentAtsu().empty());
    session.onPacket("ZGZU", {"20", "1", "XX", "LOGON ACCEPTED"});
    CHECK(session.currentAtsu().empty()); // display support does not imply state-machine support
    session.onPacket("ZGZU", {"20", "1", "NE", "LOGON ACCEPTED"});
    CHECK(session.state() == CpdlcSessionState::Active && session.currentAtsu() == "ZGZU");
    CHECK(session.pendingAtsu().empty());
    session.onPacket("ZGZU", {"21", "", "NE", "CURRENT ATC UNIT@_@ZGZU@_@GUANGZHOU CTL"});
    CHECK(session.currentAtsu() == "ZGZU");
    CHECK(session.beginLogon("ZSHA"));
    session.onPacket("ZSHA", {"22", "2", "NE", "LOGON REJECTED"});
    CHECK(session.currentAtsu() == "ZGZU" && session.pendingAtsu().empty());
    CHECK(!session.lastError().empty());
    CHECK(session.beginLogon("ZSHA"));
    CHECK(session.lastError().empty());
    session.onPacket("ZSHA", {"23", "2", "NE", "LOGON ACCEPTED"});
    CHECK(session.currentAtsu() == "ZSHA" && session.pendingAtsu().empty());
    session.onPacket("ZSHA", {"24", "", "NE", "LOGOFF"});
    CHECK(session.currentAtsu().empty() && session.state() == CpdlcSessionState::Offline);
    // A current-unit announcement may complete a pending logon only when peer and named station agree.
    CHECK(session.beginLogon("ZGZU"));
    session.onPacket("OTHER", {"25", "", "NE", "CURRENT ATC UNIT@_@ZGZU@_@GUANGZHOU CTL"});
    session.onPacket("ZGZU", {"26", "", "NE", "CURRENT ATC UNIT@_@OTHER@_@OTHER CTL"});
    CHECK(session.currentAtsu().empty());
    session.onPacket("ZGZU", {"27", "", "NE", "CURRENT ATC UNIT@_@ZGZU@_@GUANGZHOU CTL"});
    CHECK(session.currentAtsu() == "ZGZU" && session.pendingAtsu().empty());
}

void testMessageSnapshots() {
    DclRequest dcl{"ZBAA", "CCA123", "B738", "ZBAA", "ZSPD", "215", "A", "RNAV"};
    CHECK(buildDclRequest(dcl) ==
          "REQUEST PREDEP CLEARANCE CCA123 B738 TO ZSPD AT ZBAA STAND 215 ATIS A RNAV");
    CHECK(buildAltitudeRequest("fl350") == "REQUEST CLIMB TO FL350");
    CHECK(buildDescentRequest("250") == "REQUEST DESCENT TO FL250");
    CHECK(buildDirectRequest("agvos") == "REQUEST DIRECT TO AGVOS");
    CHECK(buildSpeedRequest(".80") == "REQUEST MACH .80");
    CHECK(buildSpeedRequest("280") == "REQUEST SPEED 280 KT");
    CHECK(buildWhenExpectedRequest("FL390") == "WHEN CAN WE EXPECT FL390");
    CHECK(buildPositionReport({"CCA123", "AGVOS", "1234", "350", "DUMET", "1301", "NIXAL"}) ==
          "POSITION REPORT CCA123 AGVOS AT 1234 FL350 ESTIMATING DUMET AT 1301 NEXT NIXAL");
    CHECK(buildOceanicClearance("CCA123", "B789", "DOGAL", "1345", "350", ".84",
                                "DOGAL 56N020W 57N030W") ==
          "REQUEST OCEANIC CLEARANCE CCA123 B789 ENTRY DOGAL AT 1345 FL350 M.84 VIA "
          "DOGAL 56N020W 57N030W");
    CHECK(buildInfoRequest("metar", "zbaa") == "metar ZBAA");
    CHECK(buildInfoRequest("taf", "ZSPD") == "taf ZSPD");
    CHECK(buildInfoRequest("shorttaf", "ZGGG") == "shorttaf ZGGG");
    CHECK(buildInfoRequest("vatatis", "ZBAA") == "vatatis ZBAA");
    CHECK(buildInfoRequest("ivaoatis", "ZBAA") == "ivaoatis ZBAA");
    CHECK(buildInfoRequest("peatis", "KSEA") == "peatis KSEA");
    CHECK_THROWS(buildInfoRequest("unknown", "ZBAA"));
    CHECK_THROWS(buildAltitudeRequest("900"));
    CHECK_THROWS(buildSpeedRequest("FAST"));
    CHECK(validCallsign("B-612"));
    DclRequest invalid = dcl;
    invalid.remarks = "中文";
    CHECK_THROWS(buildDclRequest(invalid));
}

void testPolling() {
    using namespace std::chrono_literals;
    const auto epoch = PollScheduler::TimePoint{};
    PollScheduler scheduler;
    CHECK(!scheduler.pollDue(epoch + 1000s));
    scheduler.recordRealRequest(epoch);
    CHECK(!scheduler.pollDue(epoch + 20s));
    CHECK(!scheduler.pollDue(epoch + 29s));
    CHECK(scheduler.pollDue(epoch + 30s));
    scheduler.recordRealRequest(epoch + 25s);
    CHECK(scheduler.nextPoll() == epoch + 30s); // sends cannot postpone polls
    scheduler.recordPollResult(epoch + 30s, true);
    CHECK(!scheduler.pollDue(epoch + 59s));
    CHECK(scheduler.pollDue(epoch + 60s));
    scheduler.recordPollResult(epoch + 60s, true);
    CHECK(scheduler.nextPoll() == epoch + 90s);

    scheduler.reset();
    CHECK(!scheduler.activated() && !scheduler.nextPoll());
    scheduler.recordRealRequest(epoch);
    scheduler.recordPollResult(epoch + 30s, false);
    CHECK(!scheduler.pollDue(epoch + 89s));
    CHECK(scheduler.pollDue(epoch + 90s));
    scheduler.recordRealRequest(epoch + 40s);
    CHECK(scheduler.nextPoll() == epoch + 90s); // preserve failure backoff
    scheduler.recordPollResult(epoch + 90s, true);
    CHECK(scheduler.nextPoll() == epoch + 120s);
}

void testAdsc() {
    using namespace std::chrono_literals;
    const auto epoch = AdscManager::TimePoint{};
    AdscManager manager(true);
    std::string response;
    CHECK(!manager.parseContractRequest("CCA123", "REQUEST PERIODIC 30", epoch, &response));
    CHECK(response == "UNABLE INTERVAL TOO SHORT");
    CHECK(manager.parseContractRequest("CCA123", "REQUEST PERIODIC 60", epoch, &response));
    CHECK(!manager.reportDue(epoch + 59s));
    CHECK(manager.reportDue(epoch + 60s));
    CHECK(buildAdscReport("CCA123", "041230", {51.4775, -0.461389, 35124.0}) ==
          "REPORT CCA123 041230 N51.47750 W0.46139 351");
    manager.reportSent(epoch + 60s);
    CHECK(!manager.reportDue(epoch + 119s));
    CHECK(manager.parseContractRequest("CCA123", "CANCEL", epoch, &response));
    CHECK(!manager.contract().active);

    manager.acceptPeriodic("CCA123", 120, epoch, nullptr);
    manager.onCallsignChanged("CCA456");
    CHECK(!manager.contract().active);
    manager.acceptPeriodic("CCA456", 120, epoch, nullptr);
    manager.onPluginDisabled();
    CHECK(!manager.contract().active);
}

void testFlightPlanPrecedence() {
    FlightPlanSnapshot manual;
    manual.destination = "ZGGG";
    manual.manualFields = {"destination"};
    FlightPlanSnapshot online{"CCA123", "B789", "ZBAA", "ZSPD", "AGVOS DUMET", {}, {}};
    FlightPlanSnapshot prefile{"CCA999", "A320", "ZSSS", "ZUCK", "PREFILE", {}, {}};
    FlightPlanSnapshot simbrief{"CCA777", "B738", "", "", "SIM ROUTE", {"AGVOS", "DUMET"}, {}};
    auto result = mergeFlightPlans(manual, online, prefile, simbrief);
    CHECK(result.callsign == "CCA123");
    CHECK(result.aircraftType == "B789");
    CHECK(result.destination == "ZGGG");
    CHECK(result.route == "AGVOS DUMET");
    CHECK(result.waypoints == std::vector<std::string>({"AGVOS", "DUMET"}));

    result = mergeFlightPlans({}, std::nullopt, prefile, simbrief);
    CHECK(result.callsign == "CCA999");
    CHECK(result.route == "PREFILE");
    result = mergeFlightPlans({}, std::nullopt, std::nullopt, simbrief);
    CHECK(result.callsign == "CCA777");
}

void testMessageAlert() {
    const auto samples = buildMessageAlertPcm();
    CHECK(messageAlertSampleRate == 48000);
    CHECK(samples.size() == 64800); // 1.35 s, rather than the old 0.2 s single tone
    if (samples.size() != 64800) return;
    CHECK(std::all_of(samples.begin(), samples.end(), [](auto sample) {
        return sample >= -9000 && sample <= 9000;
    }));
    for (int beep = 0; beep < 3; ++beep) {
        const auto start = samples.begin() + beep * 24000;
        CHECK(*start == 0 && *(start + 16799) == 0);
        CHECK(std::any_of(start, start + 16800, [](auto sample) { return sample > 8000; }));
        CHECK(std::all_of(start, start + 48, [](auto sample) { return std::abs(sample) < 900; }));
        CHECK(std::all_of(start + 16752, start + 16800,
                          [](auto sample) { return std::abs(sample) < 900; }));
        if (beep < 2)
            CHECK(std::all_of(start + 16800, start + 24000, [](auto sample) { return sample == 0; }));
    }
}

void testTranslations() {
    CHECK(translationsComplete());
    for (const auto& key : translationKeys()) {
        CHECK(translate("en", key) != key);
        CHECK(translate("zh", key) != key);
    }
    CHECK(translate("invalid", "connect") == "CONNECT");
    CHECK(translate("en", "atsu") == "Station");
    CHECK(translate("zh", "atsu") == "Station");
}

}  // namespace

int main() {
    testFormEncoding();
    testEnvelopeParser();
    testCpdlcPacketsAndReplies();
    testDclPresentationAndReplies();
    testSessionState();
    testNeSessionNotifications();
    testMessageSnapshots();
    testPolling();
    testAdsc();
    testFlightPlanPrecedence();
    testMessageAlert();
    testTranslations();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All HoppieDatalinkXP core tests passed\n";
    return EXIT_SUCCESS;
}
