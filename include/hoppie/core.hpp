#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hoppie {

struct HoppieRequest {
    std::string logon;
    std::string from;
    std::string to;
    std::string type;
    std::string packet;
    bool idempotent{false};
    bool expectsReply{false};
};

struct HoppieEnvelope {
    std::string sender;
    std::string type;
    std::string packet;
};

struct CpdlcPacket {
    std::string messageId;
    std::string replyTo;
    std::string responseKind;
    std::string body;
};

enum class MessageDirection { Sent, Received };
enum class MessageState { Sending, PendingReply, Standby, Complete, Failed };

enum class ReplyAction {
    Wilco,
    Unable,
    Affirm,
    Negative,
    Roger,
    Standby,
    Accept,
    Reject
};

struct DatalinkMessage {
    std::uint64_t localId{};
    MessageDirection direction{MessageDirection::Received};
    MessageState state{MessageState::Complete};
    std::string peer;
    std::string type;
    std::string body;
    std::optional<CpdlcPacket> cpdlc;
    bool unread{true};
    bool finalReplySent{false};
    bool replyInFlight{false};
    std::optional<ReplyAction> sentReply{}; // Updated only after Hoppie accepts the send.
};

enum class CpdlcSessionState {
    Offline,
    Connecting,
    LogonPending,
    Active,
    HandoverPending,
    LoggingOff,
    Fault
};

class CpdlcSession {
public:
    void beginConnect(std::string callsign);
    void pingSucceeded();
    void pingFailed();
    bool beginLogon(std::string atsu);
    bool beginLogoff();
    void onPacket(const std::string& sender, const CpdlcPacket& packet);
    void onSendResult(const std::string& recipient, const CpdlcPacket& packet, bool success);
    void disconnect();
    bool updateCallsign(std::string callsign);

    CpdlcSessionState state() const noexcept { return state_; }
    const std::string& callsign() const noexcept { return callsign_; }
    const std::string& currentAtsu() const noexcept { return currentAtsu_; }
    const std::string& pendingAtsu() const noexcept { return pendingAtsu_; }
    const std::string& lastError() const noexcept { return lastError_; }

private:
    CpdlcSessionState state_{CpdlcSessionState::Offline};
    std::string callsign_;
    std::string currentAtsu_;
    std::string pendingAtsu_;
    std::string lastError_;
};

struct ParseResult {
    bool ok{false};
    std::vector<HoppieEnvelope> envelopes;
    std::string error;
};

std::string trim(std::string_view text);
std::string uppercaseAscii(std::string_view text);
std::string formEncode(std::string_view value);
std::string toFormBody(const HoppieRequest& request);
ParseResult parseHoppieResponse(std::string_view response);
std::optional<CpdlcPacket> parseCpdlcPacket(std::string_view packet,
                                            std::string* error = nullptr);
bool isKnownCpdlcResponseKind(std::string_view kind);
std::string buildCpdlcPacket(std::string_view messageId,
                             std::string_view replyTo,
                             std::string_view responseKind,
                             std::string_view body);
std::vector<ReplyAction> validReplies(const CpdlcPacket& packet,
                                      bool clearanceMessage = false);
std::vector<ReplyAction> availableReplies(const DatalinkMessage& message);
bool isDclClearance(std::string_view body);
std::string replyText(ReplyAction action);
std::string buildReplyPacket(std::string_view newMessageId,
                             const CpdlcPacket& incoming,
                             ReplyAction action);
bool markFinalReply(DatalinkMessage& message, ReplyAction action);

struct MessageText {
    std::string text;
    std::vector<bool> highlighted; // One flag per UTF-8 byte, not per glyph.
};
MessageText formatMessageText(std::string_view body);

struct DclRequest {
    std::string atsu;
    std::string callsign;
    std::string aircraftType;
    std::string departure;
    std::string destination;
    std::string stand;
    std::string atis;
    std::string remarks;
};

struct PositionReport {
    std::string callsign;
    std::string currentFix;
    std::string currentTime;
    std::string flightLevel;
    std::string nextFix;
    std::string nextEta;
    std::string followingFix;
};

std::string buildDclRequest(const DclRequest& request);
std::string buildAltitudeRequest(std::string_view level);
std::string buildDescentRequest(std::string_view level);
std::string buildDirectRequest(std::string_view waypoint);
std::string buildSpeedRequest(std::string_view speedOrMach);
std::string buildWhenExpectedRequest(std::string_view target);
std::string buildPositionReport(const PositionReport& report);
std::string buildOceanicClearance(std::string_view callsign,
                                  std::string_view aircraftType,
                                  std::string_view entryPoint,
                                  std::string_view entryTime,
                                  std::string_view flightLevel,
                                  std::string_view mach,
                                  std::string_view route);
std::string buildInfoRequest(std::string_view product, std::string_view icao);
bool validCallsign(std::string_view callsign);
bool validIcao(std::string_view icao);
bool validFlightLevel(std::string_view level);

class PollScheduler {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    void reset();
    void recordRealRequest(TimePoint now);
    bool pollDue(TimePoint now) const;
    void recordPollResult(TimePoint now, bool success);
    bool activated() const noexcept { return activated_; }
    std::optional<TimePoint> nextPoll() const;

private:
    bool activated_{false};
    unsigned failureCount_{0};
    std::optional<TimePoint> regularPoll_;
};

struct AircraftSnapshot {
    double latitude{};
    double longitude{};
    double mslAltitudeFeet{};
};

struct AdscContract {
    bool enabled{false};
    bool active{false};
    unsigned intervalSeconds{};
    std::string callsign;
    std::chrono::steady_clock::time_point nextReport{};
};

class AdscManager {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    explicit AdscManager(bool userEnabled = false);
    void setUserEnabled(bool enabled);
    bool acceptPeriodic(std::string callsign, unsigned intervalSeconds,
                        TimePoint now, std::string* rejection = nullptr);
    bool parseContractRequest(std::string_view callsign, std::string_view body,
                              TimePoint now, std::string* response);
    void cancel();
    void onPluginDisabled();
    void onCallsignChanged(std::string_view callsign);
    bool reportDue(TimePoint now) const;
    void reportSent(TimePoint now);
    const AdscContract& contract() const noexcept { return contract_; }

private:
    AdscContract contract_;
};

std::string buildAdscReport(std::string_view callsign,
                            std::string_view utcDayHourMinute,
                            const AircraftSnapshot& snapshot);

struct FlightPlanSnapshot {
    std::string callsign;
    std::string aircraftType;
    std::string departure;
    std::string destination;
    std::string route;
    std::vector<std::string> waypoints;
    std::set<std::string> manualFields;
};

FlightPlanSnapshot mergeFlightPlans(const FlightPlanSnapshot& manual,
                                    const std::optional<FlightPlanSnapshot>& onlineVatsim,
                                    const std::optional<FlightPlanSnapshot>& prefileVatsim,
                                    const std::optional<FlightPlanSnapshot>& simbrief);

// Mono PCM16: three 350 ms beeps separated by 150 ms silence (1.35 seconds).
inline constexpr int messageAlertSampleRate = 48000;
std::vector<std::int16_t> buildMessageAlertPcm();

const std::vector<std::string>& translationKeys();
std::string translate(std::string_view language, std::string_view key);
bool translationsComplete();

}  // namespace hoppie
