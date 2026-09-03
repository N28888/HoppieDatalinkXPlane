#pragma once

#include "hoppie/core.hpp"
#include "hoppie/network.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hoppie {

struct Settings {
    std::string language{"en"};
    std::string vatsimCid;
    std::string simbriefId;
    std::string defaultAtisSource{"vatatis"};
    bool soundEnabled{true};
    bool adscEnabled{false};
    int windowLeft{120};
    int windowTop{850};
    int windowRight{760};
    int windowBottom{230};
};

struct SendFeedback {
    std::uint64_t messageLocalId{};
    bool success{false};
    std::string recipient;
};

class Application {
public:
    using SteadyTime = std::chrono::steady_clock::time_point;
    using SystemTime = std::chrono::system_clock::time_point;

    explicit Application(std::string settingsPath);
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void tick(const AircraftSnapshot& aircraft, SteadyTime steadyNow, SystemTime systemNow);
    void disable();

    bool connect();
    void disconnect();
    bool logon(std::string atsu);
    bool logoff();
    bool sendCpdlc(std::string body, std::string responseKind = "Y");
    bool sendDcl(DclRequest request);
    bool sendInfo(std::string product, std::string icao);
    bool sendTelex(std::string to, std::string text);
    bool reply(std::size_t messageIndex, ReplyAction action);
    bool deleteMessage(std::uint64_t localId);
    void clearMessages();
    std::optional<SendFeedback> takeSendFeedback();
    bool requestVatsim();
    bool requestSimBrief();

    void setCallsign(std::string callsign);
    void setLogonCode(std::string code, bool remember);
    void setManualField(std::string name, std::string value);
    void saveSettings() const;
    void updateSettings(Settings settings);

    bool connected() const noexcept { return connected_; }
    bool busy() const noexcept { return busy_; }
    bool secureStorageAvailable() const noexcept { return secureStorageAvailable_; }
    bool consumeSoundPending();
    bool pollInFlight() const noexcept { return pollInFlight_; }
    bool pollingActivated() const noexcept { return polling_.activated(); }
    std::optional<SteadyTime> nextPoll() const { return polling_.nextPoll(); }
    std::optional<SteadyTime> lastSuccessfulPoll() const { return lastSuccessfulPoll_; }
    std::size_t unreadCount() const;
    const std::string& statusText() const noexcept { return statusText_; }
    const std::string& logonCode() const noexcept { return logonCode_; }
    const Settings& settings() const noexcept { return settings_; }
    Settings& settings() noexcept { return settings_; }
    const CpdlcSession& session() const noexcept { return session_; }
    CpdlcSession& session() noexcept { return session_; }
    const std::vector<DatalinkMessage>& messages() const noexcept { return messages_; }
    std::vector<DatalinkMessage>& messages() noexcept { return messages_; }
    const FlightPlanSnapshot& flightPlan() const noexcept { return resolvedFlightPlan_; }

private:
    struct TaskContext {
        std::uint64_t messageLocalId{};
        bool expectsReply{false};
        std::uint64_t replyingTo{};
        ReplyAction replyAction{ReplyAction::Roger};
    };

    bool submitHoppie(HoppieRequest request, std::uint64_t replyingTo = 0,
                      ReplyAction replyAction = ReplyAction::Roger);
    void finishSend(const TaskContext& context, bool success);
    void invalidatePendingNetwork();
    bool submitGet(NetworkPurpose purpose, std::string url);
    void consumeNetwork(SteadyTime now);
    void handleHoppieResult(const NetworkResult& result, SteadyTime now);
    void handleEnvelope(const HoppieEnvelope& envelope);
    void addInbound(const HoppieEnvelope& envelope,
                    const std::optional<CpdlcPacket>& packet = std::nullopt);
    void refreshFlightPlan();
    DatalinkMessage* findMessage(std::uint64_t localId);
    DatalinkMessage* findOutgoingCpdlc(std::string_view messageId, std::string_view peer);
    std::string nextMessageId();
    static std::string utcDayHourMinute(SystemTime value);

    std::string settingsPath_;
    Settings settings_;
    std::string logonCode_;
    bool secureStorageAvailable_{true};
    bool connected_{false};
    bool busy_{false};
    bool soundPending_{false};
    bool pollInFlight_{false};
    std::string statusText_{"DISCONNECTED"};
    std::uint64_t nextTaskId_{1};
    std::uint64_t minimumAcceptedTaskId_{1};
    std::uint64_t nextLocalMessageId_{1};
    unsigned nextCpdlcId_{1};
    NetworkWorker network_;
    std::map<std::uint64_t, TaskContext> taskContexts_;
    std::vector<DatalinkMessage> messages_;
    // Protocol tracking is separate from the user-visible RX inbox. Clearing the
    // inbox must not break late replies or pending LOGON correlation.
    std::vector<DatalinkMessage> outgoingMessages_;
    std::deque<SendFeedback> sendFeedback_;
    std::set<std::pair<std::string, std::string>> receivedCpdlc_;
    PollScheduler polling_;
    SteadyTime now_{std::chrono::steady_clock::now()};
    std::optional<SteadyTime> lastSuccessfulPoll_;
    CpdlcSession session_;
    AdscManager adsc_;
    FlightPlanSnapshot manualFlightPlan_;
    FlightPlanSnapshot resolvedFlightPlan_;
    std::optional<FlightPlanSnapshot> vatsimOnline_;
    std::optional<FlightPlanSnapshot> vatsimPrefile_;
    std::optional<FlightPlanSnapshot> simbrief_;
};

}  // namespace hoppie
