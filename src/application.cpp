#include "hoppie/application.hpp"

#include "hoppie/credentials.hpp"
#include "hoppie/datasources.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hoppie {
namespace {

constexpr char hoppieEndpoint[] = "https://www.hoppie.nl/acars/system/connect.html";
constexpr char vatsimEndpoint[] = "https://data.vatsim.net/v3/vatsim-data.json";
constexpr char simbriefEndpoint[] = "https://www.simbrief.com/api/xml.fetcher.php?json=1&userid=";

std::string aviationText(std::string value) {
    value = uppercaseAscii(trim(value));
    if (value.empty() || value.size() > 1024) throw std::invalid_argument("invalid message length");
    if (!std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return c >= 0x20 && c <= 0x7e;
        })) throw std::invalid_argument("aviation messages must contain ASCII characters only");
    return value;
}

std::string aviationToken(std::string value, const char* name) {
    value = uppercaseAscii(trim(value));
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.';
        })) throw std::invalid_argument(std::string("invalid ") + name);
    return value;
}

bool successfulHttp(const NetworkResult& result) {
    return result.transportOk && result.httpStatus >= 200 && result.httpStatus < 300;
}

}  // namespace

Application::Application(std::string settingsPath)
    : settingsPath_(std::move(settingsPath)), adsc_(false) {
    try {
        std::ifstream input(std::filesystem::u8path(settingsPath_));
        if (input) {
            const auto json = nlohmann::json::parse(input);
            settings_.language = json.value("language", settings_.language);
            settings_.vatsimCid = json.value("vatsimCid", settings_.vatsimCid);
            settings_.simbriefId = json.value("simbriefId", settings_.simbriefId);
            settings_.defaultAtisSource = json.value("defaultAtisSource", settings_.defaultAtisSource);
            settings_.soundEnabled = json.value("soundEnabled", settings_.soundEnabled);
            settings_.adscEnabled = json.value("adscEnabled", settings_.adscEnabled);
            settings_.windowLeft = json.value("windowLeft", settings_.windowLeft);
            settings_.windowTop = json.value("windowTop", settings_.windowTop);
            settings_.windowRight = json.value("windowRight", settings_.windowRight);
            settings_.windowBottom = json.value("windowBottom", settings_.windowBottom);
        }
    } catch (...) {
        statusText_ = "SETTINGS INVALID — DEFAULTS LOADED";
    }
    if (settings_.language != "zh" && settings_.language != "en") settings_.language = "en";
    adsc_.setUserEnabled(settings_.adscEnabled);
    if (const auto stored = loadLogonCredential()) logonCode_ = *stored;
}

Application::~Application() { network_.stop(); }

void Application::saveSettings() const {
    if (settingsPath_.empty()) return;
    try {
        nlohmann::json json{{"language", settings_.language},
                            {"vatsimCid", settings_.vatsimCid},
                            {"simbriefId", settings_.simbriefId},
                            {"defaultAtisSource", settings_.defaultAtisSource},
                            {"soundEnabled", settings_.soundEnabled},
                            {"adscEnabled", settings_.adscEnabled},
                            {"windowLeft", settings_.windowLeft},
                            {"windowTop", settings_.windowTop},
                            {"windowRight", settings_.windowRight},
                            {"windowBottom", settings_.windowBottom}};
        const std::filesystem::path destination = std::filesystem::u8path(settingsPath_);
        std::filesystem::create_directories(destination.parent_path());
        const auto temporary = destination.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::trunc);
            if (!output) return;
            output << json.dump(2) << '\n';
        }
        std::error_code error;
        std::filesystem::remove(destination, error);
        error.clear();
        std::filesystem::rename(temporary, destination, error);
        if (error) std::filesystem::remove(temporary, error);
    } catch (...) {
    }
}

void Application::updateSettings(Settings settings) {
    const bool adscChanged = settings.adscEnabled != settings_.adscEnabled;
    settings_ = std::move(settings);
    if (settings_.language != "zh" && settings_.language != "en") settings_.language = "en";
    if (adscChanged) adsc_.setUserEnabled(settings_.adscEnabled);
    saveSettings();
}

void Application::setLogonCode(std::string code, bool remember) {
    logonCode_ = trim(code);
    secureStorageAvailable_ = remember ? saveLogonCredential(logonCode_) : deleteLogonCredential();
    if (!secureStorageAvailable_) statusText_ = "SECURE STORAGE UNAVAILABLE — CODE IS SESSION ONLY";
}

void Application::setCallsign(std::string callsign) {
    callsign = uppercaseAscii(trim(callsign));
    if (session_.callsign() == callsign) return;
    invalidatePendingNetwork();
    session_.updateCallsign(callsign);
    manualFlightPlan_.callsign = callsign;
    manualFlightPlan_.manualFields.insert("callsign");
    adsc_.onCallsignChanged(callsign);
    connected_ = false;
    lastSuccessfulPoll_.reset();
    receivedCpdlc_.clear();
    polling_.reset();
    statusText_ = "CALLSIGN CHANGED — RECONNECT REQUIRED";
    refreshFlightPlan();
}

bool Application::connect() {
    if (connected_ || busy_ || logonCode_.empty() || !validCallsign(manualFlightPlan_.callsign)) {
        statusText_ = "ENTER A VALID CALLSIGN AND HOPPIE CODE";
        return false;
    }
    session_.beginConnect(manualFlightPlan_.callsign);
    HoppieRequest request{logonCode_, session_.callsign(), "SERVER", "ping", "", true, false};
    NetworkTask task{nextTaskId_++, NetworkPurpose::Ping, HttpMethod::Post, hoppieEndpoint,
                     toFormBody(request), "application/x-www-form-urlencoded", 15};
    taskContexts_[task.id] = {};
    busy_ = network_.submit(std::move(task));
    statusText_ = busy_ ? "CONNECTING" : "NETWORK WORKER STOPPED";
    return busy_;
}

void Application::disconnect() {
    invalidatePendingNetwork();
    // Best-effort LOGOFF is a new task; it may finish after local disconnect.
    if (connected_ && session_.state() == CpdlcSessionState::Active) logoff();
    connected_ = false;
    busy_ = false;
    session_.disconnect();
    polling_.reset();
    pollInFlight_ = false;
    lastSuccessfulPoll_.reset();
    adsc_.cancel();
    statusText_ = "DISCONNECTED";
}

bool Application::logon(std::string atsu) {
    atsu = uppercaseAscii(trim(atsu));
    if (!connected_ || !validIcao(atsu)) {
        statusText_ = "ENTER A VALID STATION";
        return false;
    }
    if (!session_.beginLogon(std::move(atsu))) {
        statusText_ = "LOGON ALREADY PENDING OR SESSION NOT READY";
        return false;
    }
    return sendCpdlc("REQUEST LOGON", "Y");
}

bool Application::logoff() {
    if (!connected_ || !session_.beginLogoff()) return false;
    return sendCpdlc("LOGOFF", "N");
}

bool Application::submitHoppie(HoppieRequest request, std::uint64_t replyingTo, ReplyAction replyAction) {
    if (!connected_ && request.type != "ping") {
        statusText_ = "NOT CONNECTED";
        return false;
    }
    request.from = session_.callsign();
    request.logon = logonCode_;
    if (trim(request.to).empty()) {
        statusText_ = "RECIPIENT REQUIRED";
        return false;
    }
    outgoingMessages_.push_back({nextLocalMessageId_++, MessageDirection::Sent, MessageState::Sending,
                                 request.to, request.type, request.packet, std::nullopt, false, false});
    auto& outgoing = outgoingMessages_.back();
    if (request.type == "cpdlc") {
        outgoing.cpdlc = parseCpdlcPacket(request.packet);
        if (outgoing.cpdlc) outgoing.body = outgoing.cpdlc->body;
    }
    NetworkTask task{nextTaskId_++, NetworkPurpose::Send, HttpMethod::Post, hoppieEndpoint,
                     toFormBody(request), "application/x-www-form-urlencoded", 15};
    const auto taskId = task.id;
    taskContexts_[taskId] = {outgoing.localId, request.expectsReply, replyingTo, replyAction};
    if (!network_.submit(std::move(task))) {
        finishSend(taskContexts_.at(taskId), false);
        taskContexts_.erase(taskId);
        statusText_ = "NETWORK WORKER STOPPED";
        return false;
    }
    polling_.recordRealRequest(now_);
    statusText_ = "SENDING TO " + request.to;
    return true;
}

void Application::finishSend(const TaskContext& context, bool success) {
    auto* outgoing = findMessage(context.messageLocalId);
    if (!outgoing) return;
    outgoing->state = success ? (context.expectsReply ? MessageState::PendingReply : MessageState::Complete)
                              : MessageState::Failed;
    if (outgoing->cpdlc) session_.onSendResult(outgoing->peer, *outgoing->cpdlc, success);
    if (auto* original = findMessage(context.replyingTo)) {
        original->replyInFlight = false;
        if (success) markFinalReply(*original, context.replyAction);
        else original->state = MessageState::Failed;
    }
    sendFeedback_.push_back({outgoing->localId, success, outgoing->peer});
}

void Application::invalidatePendingNetwork() {
    // Queued work is discarded. A request already inside the OS transport cannot
    // safely be recalled; ignore its eventual result and mark its outcome unknown.
    network_.discardPending();
    for (const auto& item : taskContexts_)
        if (item.second.messageLocalId != 0) finishSend(item.second, false);
    taskContexts_.clear();
    minimumAcceptedTaskId_ = nextTaskId_;
    busy_ = false;
    pollInFlight_ = false;
}

bool Application::sendCpdlc(std::string body, std::string responseKind) {
    try {
        body = aviationText(std::move(body));
        auto target = session_.state() == CpdlcSessionState::LogonPending ||
                              session_.state() == CpdlcSessionState::HandoverPending
                          ? session_.pendingAtsu()
                          : session_.currentAtsu();
        if (target.empty()) {
            statusText_ = "NO STATION SELECTED";
            return false;
        }
        const bool expectsReply = uppercaseAscii(responseKind) != "N";
        HoppieRequest request{"", session_.callsign(), target, "cpdlc",
                              buildCpdlcPacket(nextMessageId(), "", responseKind, body),
                              false, expectsReply};
        return submitHoppie(std::move(request));
    } catch (const std::exception& error) {
        statusText_ = error.what();
        return false;
    }
}

bool Application::sendDcl(DclRequest request) {
    try {
        if (!validIcao(request.atsu)) throw std::invalid_argument("invalid Station");
        // The connected identity, not an imported or stale draft, owns the clearance.
        request.callsign = session_.callsign();
        HoppieRequest wire{"", session_.callsign(), uppercaseAscii(trim(request.atsu)), "telex",
                           buildDclRequest(request), false, true};
        return submitHoppie(std::move(wire));
    } catch (const std::exception& error) {
        statusText_ = error.what();
        return false;
    }
}

bool Application::sendInfo(std::string product, std::string icao) {
    try {
        HoppieRequest request{"", session_.callsign(), "SERVER", "inforeq",
                              buildInfoRequest(product, icao), true, true};
        return submitHoppie(std::move(request));
    } catch (const std::exception& error) {
        statusText_ = error.what();
        return false;
    }
}

bool Application::sendTelex(std::string to, std::string text) {
    try {
        HoppieRequest request{"", session_.callsign(), aviationToken(std::move(to), "recipient"), "telex",
                              aviationText(std::move(text)), false, false};
        return submitHoppie(std::move(request));
    } catch (const std::exception& error) {
        statusText_ = error.what();
        return false;
    }
}

bool Application::reply(std::size_t messageIndex, ReplyAction action) {
    if (messageIndex >= messages_.size()) return false;
    auto& message = messages_[messageIndex];
    const auto options = availableReplies(message);
    if (std::find(options.begin(), options.end(), action) == options.end()) return false;
    const auto localId = message.localId;
    const auto incoming = *message.cpdlc;
    HoppieRequest request{"", session_.callsign(), message.peer, "cpdlc",
                          buildReplyPacket(nextMessageId(), incoming, action), false, false};
    if (!submitHoppie(std::move(request), localId, action)) return false;
    auto* original = findMessage(localId);
    if (!original) return false;
    original->replyInFlight = true;
    original->state = MessageState::Sending;
    return true;
}

bool Application::deleteMessage(std::uint64_t localId) {
    const auto found = std::find_if(messages_.begin(), messages_.end(), [=](const auto& message) {
        return message.localId == localId && message.direction == MessageDirection::Received;
    });
    if (found == messages_.end()) return false;
    messages_.erase(found);
    if (unreadCount() == 0) soundPending_ = false;
    return true;
}

void Application::clearMessages() {
    messages_.clear();
    soundPending_ = false;
    // Keep received IDs and outbound protocol tracking: delayed/duplicate replies
    // must not resurrect deleted messages or break a pending ATSU logon.
}

std::optional<SendFeedback> Application::takeSendFeedback() {
    if (sendFeedback_.empty()) return std::nullopt;
    auto result = std::move(sendFeedback_.front());
    sendFeedback_.pop_front();
    return result;
}

bool Application::submitGet(NetworkPurpose purpose, std::string url) {
    NetworkTask task{nextTaskId_++, purpose, HttpMethod::Get, std::move(url), {}, {}, 15};
    taskContexts_[task.id] = {};
    return network_.submit(std::move(task));
}

bool Application::requestVatsim() {
    if (settings_.vatsimCid.empty()) {
        statusText_ = "ENTER VATSIM CID";
        return false;
    }
    statusText_ = "FETCHING VATSIM";
    return submitGet(NetworkPurpose::Vatsim, vatsimEndpoint);
}

bool Application::requestSimBrief() {
    if (settings_.simbriefId.empty() || !std::all_of(settings_.simbriefId.begin(),
            settings_.simbriefId.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        statusText_ = "ENTER NUMERIC SIMBRIEF PILOT ID";
        return false;
    }
    statusText_ = "FETCHING SIMBRIEF";
    return submitGet(NetworkPurpose::SimBrief,
                     std::string(simbriefEndpoint) + formEncode(settings_.simbriefId));
}

void Application::tick(const AircraftSnapshot& aircraft, SteadyTime steadyNow, SystemTime systemNow) {
    now_ = steadyNow;
    consumeNetwork(steadyNow);
    if (connected_ && polling_.pollDue(steadyNow) && !pollInFlight_) {
        HoppieRequest poll{logonCode_, session_.callsign(), "SERVER", "poll", "", true, false};
        NetworkTask task{nextTaskId_++, NetworkPurpose::Poll, HttpMethod::Post, hoppieEndpoint,
                         toFormBody(poll), "application/x-www-form-urlencoded", 15};
        taskContexts_[task.id] = {};
        pollInFlight_ = network_.submit(std::move(task));
        if (!pollInFlight_) polling_.recordPollResult(steadyNow, false);
    }
    if (connected_ && !session_.currentAtsu().empty() && adsc_.reportDue(steadyNow)) {
        HoppieRequest report{"", session_.callsign(), session_.currentAtsu(), "ads-c",
                             buildAdscReport(session_.callsign(), utcDayHourMinute(systemNow), aircraft),
                             false, false};
        if (submitHoppie(std::move(report))) adsc_.reportSent(steadyNow);
    }
}

void Application::consumeNetwork(SteadyTime now) {
    NetworkResult result;
    while (network_.tryPop(result)) {
        if (result.id < minimumAcceptedTaskId_) continue;
        if (result.purpose == NetworkPurpose::Ping) busy_ = false;
        if (result.purpose == NetworkPurpose::Vatsim) {
            if (!successfulHttp(result)) {
                statusText_ = "VATSIM FETCH FAILED: " + result.errorCategory;
            } else {
                try {
                    const auto plans = parseVatsimData(result.body, settings_.vatsimCid);
                    vatsimOnline_ = plans.online;
                    vatsimPrefile_ = plans.prefile;
                    refreshFlightPlan();
                    statusText_ = plans.online ? "VATSIM ONLINE FLIGHT LOADED"
                                               : plans.prefile ? "VATSIM PREFILE LOADED"
                                                               : "VATSIM FLIGHT NOT FOUND";
                } catch (...) {
                    statusText_ = "VATSIM RESPONSE INVALID";
                }
            }
        } else if (result.purpose == NetworkPurpose::SimBrief) {
            if (!successfulHttp(result)) {
                statusText_ = "SIMBRIEF FETCH FAILED: " + result.errorCategory;
            } else {
                try {
                    simbrief_ = parseSimBriefOfp(result.body);
                    refreshFlightPlan();
                    statusText_ = simbrief_ ? "SIMBRIEF OFP LOADED" : "SIMBRIEF OFP NOT FOUND";
                } catch (...) {
                    statusText_ = "SIMBRIEF RESPONSE INVALID";
                }
            }
        } else {
            handleHoppieResult(result, now);
        }
        taskContexts_.erase(result.id);
    }
}

void Application::handleHoppieResult(const NetworkResult& result, SteadyTime now) {
    const auto context = taskContexts_.find(result.id);
    if (context == taskContexts_.end()) return;
    if (result.purpose == NetworkPurpose::Poll) pollInFlight_ = false;
    if (!successfulHttp(result)) {
        if (result.purpose == NetworkPurpose::Send) finishSend(context->second, false);
        if (result.purpose == NetworkPurpose::Ping) {
            connected_ = false;
            session_.pingFailed();
        }
        if (result.purpose == NetworkPurpose::Poll) polling_.recordPollResult(now, false);
        statusText_ = (result.purpose == NetworkPurpose::Poll ? "POLL FAILED — WILL RETRY: " : "NETWORK ERROR: ") +
                      (result.errorCategory.empty() ? std::to_string(result.httpStatus)
                                                    : result.errorCategory);
        return;
    }
    const auto parsed = parseHoppieResponse(result.body);
    if (!parsed.ok) {
        // A later broken envelope must not discard earlier complete deliveries.
        for (const auto& envelope : parsed.envelopes) handleEnvelope(envelope);
        if (result.purpose == NetworkPurpose::Send) finishSend(context->second, false);
        if (result.purpose == NetworkPurpose::Ping) {
            connected_ = false;
            session_.pingFailed();
        }
        if (result.purpose == NetworkPurpose::Poll) polling_.recordPollResult(now, false);
        statusText_ = (result.purpose == NetworkPurpose::Poll ? "POLL FAILED — WILL RETRY: " : "HOPPIE ERROR: ") + parsed.error;
        return;
    }
    if (result.purpose == NetworkPurpose::Ping) {
        connected_ = true;
        session_.pingSucceeded();
        statusText_ = "CONNECTED — READY FOR LOGON";
    } else {
        if (result.purpose == NetworkPurpose::Send) finishSend(context->second, true);
        if (result.purpose == NetworkPurpose::Poll) {
            polling_.recordPollResult(now, true);
            lastSuccessfulPoll_ = now;
            statusText_ = "CONNECTED — POLL OK";
        }
        for (const auto& envelope : parsed.envelopes) handleEnvelope(envelope);
        if (parsed.envelopes.empty() && result.purpose == NetworkPurpose::Send)
            statusText_ = "MESSAGE SENT";
    }
}

void Application::handleEnvelope(const HoppieEnvelope& envelope) {
    const auto envelopeType = uppercaseAscii(envelope.type);
    if (envelopeType == "CPDLC") {
        // Controllers can reuse IDs after restarting. Only an identical retransmission
        // from the same sender is a duplicate; different bodies/kinds/associations survive.
        if (!receivedCpdlc_.emplace(uppercaseAscii(trim(envelope.sender)), envelope.packet).second) return;
        const auto packet = parseCpdlcPacket(envelope.packet);
        if (!packet) {
            addInbound(envelope); // Raw, read-only fallback: receipt never depends on parser support.
            return;
        }
        if (isKnownCpdlcResponseKind(packet->responseKind) && !packet->replyTo.empty()) {
            if (auto* sent = findOutgoingCpdlc(packet->replyTo, envelope.sender)) {
                sent->state = uppercaseAscii(packet->body) == "STANDBY" ? MessageState::Standby
                                                                        : MessageState::Complete;
            }
        }
        session_.onPacket(envelope.sender, *packet);
        addInbound(envelope, packet);
        return;
    }
    if (envelopeType == "ADS-C") {
        std::string response;
        const bool accepted = adsc_.parseContractRequest(session_.callsign(), envelope.packet,
                                                         now_, &response);
        addInbound(envelope);
        HoppieRequest reply{"", session_.callsign(), envelope.sender, "ads-c", response, false, false};
        submitHoppie(std::move(reply));
        statusText_ = accepted ? "ADS-C PERIODIC CONTRACT ACTIVE" : response;
        return;
    }
    if (envelopeType == "INFOREQ" || envelopeType == "DATA") {
        const auto pending = std::find_if(outgoingMessages_.rbegin(), outgoingMessages_.rend(), [](const auto& message) {
            return message.direction == MessageDirection::Sent && message.type == "inforeq" &&
                   message.state == MessageState::PendingReply;
        });
        if (pending != outgoingMessages_.rend()) pending->state = MessageState::Complete;
        auto data = envelope;
        data.type = "DATA";
        addInbound(data);
    } else {
        const auto body = uppercaseAscii(envelope.packet);
        if (envelopeType == "TELEX" &&
            (body.find("CLEARANCE") != std::string::npos || body.find("ACCEPT") != std::string::npos ||
             body.find("REJECT") != std::string::npos)) {
            const auto pending = std::find_if(outgoingMessages_.rbegin(), outgoingMessages_.rend(), [&](const auto& message) {
                return message.direction == MessageDirection::Sent && message.type == "telex" &&
                       message.state == MessageState::PendingReply &&
                       uppercaseAscii(message.peer) == uppercaseAscii(envelope.sender);
            });
            if (pending != outgoingMessages_.rend()) pending->state = MessageState::Complete;
        }
        addInbound(envelope);
    }
}

void Application::addInbound(const HoppieEnvelope& envelope,
                             const std::optional<CpdlcPacket>& packet) {
    messages_.push_back({nextLocalMessageId_++, MessageDirection::Received,
                         packet && !validReplies(*packet).empty() ? MessageState::PendingReply
                                                                  : MessageState::Complete,
                         uppercaseAscii(envelope.sender), envelope.type,
                         packet ? packet->body : envelope.packet, packet, true, false});
    soundPending_ = settings_.soundEnabled;
    statusText_ = "NEW MESSAGE";
}

void Application::setManualField(std::string name, std::string value) {
    value = uppercaseAscii(trim(value));
    if (name == "callsign") manualFlightPlan_.callsign = value;
    else if (name == "aircraftType") manualFlightPlan_.aircraftType = value;
    else if (name == "departure") manualFlightPlan_.departure = value;
    else if (name == "destination") manualFlightPlan_.destination = value;
    else if (name == "route") manualFlightPlan_.route = value;
    else return;
    manualFlightPlan_.manualFields.insert(name);
    refreshFlightPlan();
}

void Application::refreshFlightPlan() {
    resolvedFlightPlan_ = mergeFlightPlans(manualFlightPlan_, vatsimOnline_, vatsimPrefile_, simbrief_);
}

DatalinkMessage* Application::findMessage(std::uint64_t localId) {
    const auto found = std::find_if(messages_.begin(), messages_.end(), [=](const auto& message) {
        return message.localId == localId;
    });
    if (found != messages_.end()) return &*found;
    const auto sent = std::find_if(outgoingMessages_.begin(), outgoingMessages_.end(), [=](const auto& message) {
        return message.localId == localId;
    });
    return sent == outgoingMessages_.end() ? nullptr : &*sent;
}

DatalinkMessage* Application::findOutgoingCpdlc(std::string_view messageId, std::string_view peer) {
    const auto found = std::find_if(outgoingMessages_.begin(), outgoingMessages_.end(), [&](const auto& message) {
        return message.direction == MessageDirection::Sent && message.cpdlc &&
               message.cpdlc->messageId == messageId && message.peer == uppercaseAscii(trim(peer));
    });
    return found == outgoingMessages_.end() ? nullptr : &*found;
}

std::string Application::nextMessageId() {
    const auto result = std::to_string(nextCpdlcId_++);
    if (nextCpdlcId_ > 999999) nextCpdlcId_ = 1;
    return result;
}

std::string Application::utcDayHourMinute(SystemTime value) {
    const auto time = std::chrono::system_clock::to_time_t(value);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    char buffer[7]{};
    std::snprintf(buffer, sizeof(buffer), "%02d%02d%02d", utc.tm_mday, utc.tm_hour, utc.tm_min);
    return buffer;
}

bool Application::consumeSoundPending() {
    const bool value = soundPending_;
    soundPending_ = false;
    return value;
}

std::size_t Application::unreadCount() const {
    return static_cast<std::size_t>(std::count_if(messages_.begin(), messages_.end(),
                                                  [](const auto& message) { return message.unread; }));
}

void Application::disable() {
    invalidatePendingNetwork();
    connected_ = false;
    session_.disconnect();
    polling_.reset();
    pollInFlight_ = false;
    lastSuccessfulPoll_.reset();
    adsc_.onPluginDisabled();
    statusText_ = "PLUGIN DISABLED";
    saveSettings();
}

}  // namespace hoppie
