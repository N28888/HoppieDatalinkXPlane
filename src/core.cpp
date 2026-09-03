#include "hoppie/core.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace hoppie {
namespace {

bool isTokenChar(char value) {
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '-' ||
           value == '_' || value == '.';
}

std::string requireUpperToken(std::string_view value, const char* name) {
    const auto token = uppercaseAscii(trim(value));
    if (token.empty() ||
        !std::all_of(token.begin(), token.end(), [](char c) { return isTokenChar(c); })) {
        throw std::invalid_argument(std::string(name) + " is invalid");
    }
    return token;
}

std::string requireAviationText(std::string_view value, const char* name) {
    const auto text = uppercaseAscii(trim(value));
    if (text.empty() || text.size() > 1024 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return c >= 0x20 && c <= 0x7e;
        })) throw std::invalid_argument(std::string(name) + " is invalid");
    return text;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool finalAction(ReplyAction action) {
    return action != ReplyAction::Standby;
}

std::string formatCoordinate(double value, bool latitude) {
    const char positive = latitude ? 'N' : 'E';
    const char negative = latitude ? 'S' : 'W';
    std::ostringstream out;
    out << (value < 0.0 ? negative : positive) << std::fixed << std::setprecision(5)
        << std::abs(value);
    return out.str();
}

void overlayField(std::string& result, const std::string& source) {
    if (result.empty() && !source.empty()) result = source;
}

using Dictionary = std::map<std::string, std::string>;

const std::map<std::string, Dictionary>& dictionaries() {
    static const std::map<std::string, Dictionary> value{
        {"en", {{"title", "Hoppie Datalink XP"}, {"status", "STATUS"},
                 {"messages", "MESSAGES"}, {"atc", "ATC"},
                 {"request", "REQUEST"}, {"wx_telex", "WX / TELEX"},
                 {"settings", "SETTINGS"}, {"connect", "CONNECT"},
                 {"disconnect", "DISCONNECT"}, {"logon", "LOGON"},
                 {"logoff", "LOGOFF"}, {"handover", "HANDOVER"}, {"send", "SEND"},
                 {"refresh", "REFRESH"}, {"callsign", "CALLSIGN"},
                 {"atsu", "Station"}, {"language", "LANGUAGE"},
                 {"sound", "SOUND"}, {"adsc", "ADS-C"},
                 {"warning", "SIMULATION ONLY — MAINTAIN VOICE WATCH"},
                 {"no_messages", "NO MESSAGES"}, {"standby", "STANDBY"},
                 {"wilco", "WILCO"}, {"unable", "UNABLE"},
                 {"affirm", "AFFIRM"}, {"negative", "NEGATIVE"},
                 {"roger", "ROGER"}, {"accept", "ACCEPT"},
                 {"reject", "REJECT"}, {"save", "SAVE"},
                 {"fetch_vatsim", "FETCH VATSIM"},
                 {"fetch_simbrief", "FETCH SIMBRIEF"},
                 {"mark_read", "MARK READ"},
                 {"delete_message", "DELETE"}, {"clear_messages", "CLEAR ALL"},
                 {"inbox_only", "RECEIVED MESSAGES ONLY"},
                 {"send_success", "SENT — ACCEPTED BY HOPPIE (NOT AN ATC ACKNOWLEDGEMENT)"},
                 {"send_failed", "SEND FAILED / UNCONFIRMED — CHECK BEFORE RETRYING"}}},
        {"zh", {{"title", "Hoppie 数据链"}, {"status", "状态"},
                 {"messages", "报文"}, {"atc", "管制"},
                 {"request", "请求"}, {"wx_telex", "天气 / 电传"},
                 {"settings", "设置"}, {"connect", "连接"},
                 {"disconnect", "断开"}, {"logon", "登录"},
                 {"logoff", "登出"}, {"handover", "移交"}, {"send", "发送"},
                 {"refresh", "刷新"}, {"callsign", "呼号"},
                 {"atsu", "Station"}, {"language", "语言"},
                 {"sound", "提示音"}, {"adsc", "ADS-C"},
                 {"warning", "仅供飞行模拟 — 请保持语音守听"},
                 {"no_messages", "暂无报文"}, {"standby", "稍候"},
                 {"wilco", "照办"}, {"unable", "无法"},
                 {"affirm", "是"}, {"negative", "否"},
                 {"roger", "收到"}, {"accept", "接受"},
                 {"reject", "拒绝"}, {"save", "保存"},
                 {"fetch_vatsim", "读取 VATSIM"},
                 {"fetch_simbrief", "读取 SimBrief"},
                 {"mark_read", "标为已读"},
                 {"delete_message", "删除"}, {"clear_messages", "清空全部"},
                 {"inbox_only", "仅显示收到的报文"},
                 {"send_success", "发送成功 — Hoppie 已接收（不代表管制确认）"},
                 {"send_failed", "发送失败或结果未知 — 核实后再重发"}}}
    };
    return value;
}

}  // namespace

std::string trim(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return std::string(text.substr(first, last - first));
}

std::string uppercaseAscii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    return result;
}

std::string formEncode(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            result.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            result.push_back('+');
        } else {
            result.push_back('%');
            result.push_back(hex[c >> 4]);
            result.push_back(hex[c & 0x0F]);
        }
    }
    return result;
}

std::string toFormBody(const HoppieRequest& request) {
    // Server-directed requests still require a non-empty destination (Hoppie API).
    const auto to = trim(request.to).empty() &&
                        (request.type == "poll" || request.type == "ping" ||
                         request.type == "inforeq") ? std::string("SERVER") : request.to;
    return "logon=" + formEncode(request.logon) + "&from=" + formEncode(request.from) +
           "&to=" + formEncode(to) + "&type=" + formEncode(request.type) +
           "&packet=" + formEncode(request.packet);
}

ParseResult parseHoppieResponse(std::string_view response) {
    ParseResult result;
    const auto input = trim(response);
    if (startsWith(input, "error")) {
        result.error = trim(std::string_view(input).substr(5));
        if (result.error.empty()) result.error = "Hoppie returned an unspecified error";
        return result;
    }
    if (!startsWith(input, "ok") || (input.size() > 2 && !std::isspace(
            static_cast<unsigned char>(input[2])) && input[2] != '{')) {
        result.error = "response does not start with ok or error";
        return result;
    }

    std::size_t cursor = 2;
    auto skipSpace = [&] {
        while (cursor < input.size() &&
               std::isspace(static_cast<unsigned char>(input[cursor]))) ++cursor;
    };
    auto token = [&]() -> std::string {
        skipSpace();
        const auto start = cursor;
        while (cursor < input.size() && input[cursor] != '{' && input[cursor] != '}' &&
               !std::isspace(static_cast<unsigned char>(input[cursor]))) ++cursor;
        return std::string(input.substr(start, cursor - start));
    };
    auto braced = [&]() -> std::optional<std::string> {
        skipSpace();
        if (cursor >= input.size() || input[cursor] != '{') return std::nullopt;
        const auto start = ++cursor;
        int depth = 1;
        while (cursor < input.size()) {
            if (input[cursor] == '{') ++depth;
            if (input[cursor] == '}' && --depth == 0) {
                const auto value = std::string(input.substr(start, cursor - start));
                ++cursor;
                return value;
            }
            ++cursor;
        }
        return std::nullopt;
    };

    skipSpace();
    while (cursor < input.size()) {
        if (input[cursor] != '{') {
            result.error = "unexpected text after ok";
            return result;
        }
        ++cursor;
        auto sender = token();
        auto type = token();
        skipSpace();
        // Some Hoppie responses prefix each envelope with a numeric server ID.
        if (cursor < input.size() && input[cursor] != '{' && !sender.empty() &&
            std::all_of(sender.begin(), sender.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            sender = type;
            type = token();
        }
        auto packet = braced();
        skipSpace();
        if (sender.empty() || type.empty() || !packet || cursor >= input.size() ||
            input[cursor] != '}') {
            result.error = "malformed Hoppie envelope";
            return result;
        }
        ++cursor;
        result.envelopes.push_back({sender, type, *packet});
        skipSpace();
    }
    result.ok = true;
    return result;
}

std::optional<CpdlcPacket> parseCpdlcPacket(std::string_view packet, std::string* error) {
    if (error) error->clear();
    // Accept either header case without changing the received body.
    while (!packet.empty() && std::isspace(static_cast<unsigned char>(packet.front()))) packet.remove_prefix(1);
    constexpr std::string_view prefix = "/data2/";
    if (uppercaseAscii(packet.substr(0, prefix.size())) != "/DATA2/") {
        if (error) *error = "not a /data2 packet";
        return std::nullopt;
    }
    std::array<std::string, 4> fields;
    std::size_t cursor = prefix.size();
    for (std::size_t index = 0; index < 3; ++index) {
        const auto slash = packet.find('/', cursor);
        if (slash == std::string_view::npos) {
            if (error) *error = "missing /data2 field";
            return std::nullopt;
        }
        fields[index] = std::string(packet.substr(cursor, slash - cursor));
        cursor = slash + 1;
    }
    fields[3] = std::string(packet.substr(cursor));
    fields[0] = trim(fields[0]);
    fields[1] = trim(fields[1]);
    const auto numericId = [](const std::string& value) {
        return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    if (!numericId(fields[0]) || (!fields[1].empty() && !numericId(fields[1]))) {
        if (error) *error = "invalid message or reply ID";
        return std::nullopt;
    }
    // Receive unknown kinds too. Reply eligibility, not parsing, is allowlisted.
    const auto kind = uppercaseAscii(trim(fields[2]));
    return CpdlcPacket{fields[0], fields[1], kind, fields[3]};
}

bool isKnownCpdlcResponseKind(std::string_view kind) {
    return kind == "AN" || kind == "WU" || kind == "R" || kind == "Y" || kind == "N" || kind == "NE";
}

std::string buildCpdlcPacket(std::string_view messageId, std::string_view replyTo,
                             std::string_view responseKind, std::string_view body) {
    const auto id = requireUpperToken(messageId, "message ID");
    const auto kind = requireUpperToken(responseKind, "response kind");
    return "/data2/" + id + "/" + uppercaseAscii(trim(replyTo)) + "/" + kind + "/" +
           uppercaseAscii(trim(body));
}

std::vector<ReplyAction> validReplies(const CpdlcPacket& packet, bool clearanceMessage) {
    if (packet.responseKind == "N" || packet.responseKind == "NE") return {};
    if (clearanceMessage && (packet.responseKind == "WU" || packet.responseKind == "AN" ||
                             packet.responseKind == "R" || packet.responseKind == "Y"))
        return {ReplyAction::Accept};
    if (packet.responseKind == "WU")
        return {ReplyAction::Standby, ReplyAction::Wilco, ReplyAction::Unable};
    if (packet.responseKind == "AN")
        return {ReplyAction::Standby, ReplyAction::Affirm, ReplyAction::Negative};
    if (packet.responseKind == "R")
        return {ReplyAction::Standby, ReplyAction::Roger, ReplyAction::Unable};
    if (packet.responseKind == "Y")
        return {ReplyAction::Standby, ReplyAction::Roger, ReplyAction::Unable};
    return {};
}

std::vector<ReplyAction> availableReplies(const DatalinkMessage& message) {
    if (message.direction != MessageDirection::Received || !message.cpdlc ||
        message.finalReplySent || message.replyInFlight) return {};
    return validReplies(*message.cpdlc, isDclClearance(message.body));
}

MessageText formatMessageText(std::string_view body) {
    MessageText result;
    result.text.reserve(body.size());
    result.highlighted.reserve(body.size());
    bool highlight = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '@') {
            // Hide all markers, but only highlight a matched pair. Never drop body text.
            highlight = !highlight && body.find('@', i + 1) != std::string_view::npos;
        } else {
            result.text += body[i];
            result.highlighted.push_back(highlight);
        }
    }
    return result;
}

bool isDclClearance(std::string_view body) {
    // Recognize an actual departure clearance, not any mention of "CLEARANCE"
    // (requests, logon replies and oceanic clearances use their ordinary CPDLC options).
    std::istringstream words(uppercaseAscii(formatMessageText(body).text));
    std::string normalized = " ";
    for (std::string word; words >> word;) normalized += word + " ";
    const bool departure = startsWith(normalized, " CLD ") ||
        normalized.find(" PDC ") != std::string::npos || normalized.find(" DCL ") != std::string::npos ||
        normalized.find(" PREDEP CLEARANCE ") != std::string::npos ||
        normalized.find(" PRE-DEPARTURE CLEARANCE ") != std::string::npos ||
        normalized.find(" DEPARTURE CLEARANCE ") != std::string::npos;
    return departure && (normalized.find(" CLR TO ") != std::string::npos ||
                          normalized.find(" CLRD TO ") != std::string::npos ||
                          normalized.find(" CLEARED TO ") != std::string::npos);
}

std::string replyText(ReplyAction action) {
    switch (action) {
        case ReplyAction::Wilco: return "WILCO";
        case ReplyAction::Unable: return "UNABLE";
        case ReplyAction::Affirm: return "AFFIRM";
        case ReplyAction::Negative: return "NEGATIVE";
        case ReplyAction::Roger: return "ROGER";
        case ReplyAction::Standby: return "STANDBY";
        case ReplyAction::Accept: return "ACCEPT";
        case ReplyAction::Reject: return "REJECT";
    }
    return {};
}

std::string buildReplyPacket(std::string_view newMessageId, const CpdlcPacket& incoming,
                             ReplyAction action) {
    // ACCEPT is the DCL UI action; retain the positive response requested on the wire.
    if (action == ReplyAction::Accept) {
        if (incoming.responseKind == "WU") action = ReplyAction::Wilco;
        else if (incoming.responseKind == "AN")
            return buildCpdlcPacket(newMessageId, incoming.messageId, "N", "AFFIRMATIVE");
        else if (incoming.responseKind == "R" || incoming.responseKind == "Y") action = ReplyAction::Roger;
        else throw std::invalid_argument("message does not allow acceptance");
    }
    return buildCpdlcPacket(newMessageId, incoming.messageId, "N", replyText(action));
}

bool markFinalReply(DatalinkMessage& message, ReplyAction action) {
    if (message.direction != MessageDirection::Received || message.finalReplySent) return false;
    message.replyInFlight = false;
    message.sentReply = action;
    if (finalAction(action)) message.finalReplySent = true;
    message.state = action == ReplyAction::Standby ? MessageState::Standby : MessageState::Complete;
    return true;
}

void CpdlcSession::beginConnect(std::string callsign) {
    callsign_ = uppercaseAscii(trim(callsign));
    currentAtsu_.clear();
    pendingAtsu_.clear();
    lastError_.clear();
    state_ = validCallsign(callsign_) ? CpdlcSessionState::Connecting : CpdlcSessionState::Fault;
    if (state_ == CpdlcSessionState::Fault) lastError_ = "INVALID CALLSIGN";
}

void CpdlcSession::pingSucceeded() {
    if (state_ == CpdlcSessionState::Connecting) state_ = CpdlcSessionState::Offline;
}

void CpdlcSession::pingFailed() {
    state_ = CpdlcSessionState::Fault;
    lastError_ = "PING FAILED";
}

bool CpdlcSession::beginLogon(std::string atsu) {
    if (state_ != CpdlcSessionState::Offline && state_ != CpdlcSessionState::Active) return false;
    pendingAtsu_ = uppercaseAscii(trim(atsu));
    if (pendingAtsu_.empty()) return false;
    lastError_.clear();
    state_ = currentAtsu_.empty() ? CpdlcSessionState::LogonPending
                                 : CpdlcSessionState::HandoverPending;
    return true;
}

bool CpdlcSession::beginLogoff() {
    if (state_ != CpdlcSessionState::Active) return false;
    state_ = CpdlcSessionState::LoggingOff;
    return true;
}

void CpdlcSession::onPacket(const std::string& sender, const CpdlcPacket& packet) {
    if (!isKnownCpdlcResponseKind(packet.responseKind)) return;
    auto presentation = packet.body;
    std::replace(presentation.begin(), presentation.end(), '@', ' ');
    std::replace(presentation.begin(), presentation.end(), '_', ' ');
    std::istringstream words(uppercaseAscii(presentation));
    std::string body, word;
    while (words >> word) {
        if (!body.empty()) body += ' ';
        body += word;
    }
    const auto peer = uppercaseAscii(trim(sender));
    std::string announcedStation;
    if (startsWith(body, "CURRENT ATC UNIT ")) {
        std::istringstream announcement(body.substr(17));
        announcement >> announcedStation;
    }
    if ((state_ == CpdlcSessionState::LogonPending ||
         state_ == CpdlcSessionState::HandoverPending) &&
        peer == pendingAtsu_ &&
        (body == "LOGON ACCEPTED" || startsWith(body, "LOGON ACCEPTED ") || body == "ACCEPT" ||
         body == "ACCEPTED" || announcedStation == pendingAtsu_)) {
        currentAtsu_ = pendingAtsu_.empty() ? uppercaseAscii(sender) : pendingAtsu_;
        pendingAtsu_.clear();
        lastError_.clear();
        state_ = CpdlcSessionState::Active;
        return;
    }
    if ((state_ == CpdlcSessionState::LogonPending ||
         state_ == CpdlcSessionState::HandoverPending) &&
        peer == pendingAtsu_ &&
        (body.find("REJECT") != std::string::npos || body.find("UNABLE") != std::string::npos)) {
        pendingAtsu_.clear();
        if (currentAtsu_.empty()) {
            state_ = CpdlcSessionState::Offline;
        } else {
            state_ = CpdlcSessionState::Active;
        }
        lastError_ = body;
        return;
    }
    if (currentAtsu_.empty() || peer != currentAtsu_) return;
    for (const auto prefix : {std::string_view("HANDOVER TO "), std::string_view("HANDOVER "),
                              std::string_view("NEXT ATC UNIT ")}) {
        if (startsWith(body, prefix)) {
            const auto target = trim(std::string_view(body).substr(prefix.size()));
            if (!validIcao(target)) return;
            pendingAtsu_ = target;
            state_ = CpdlcSessionState::HandoverPending;
            return;
        }
    }
    if (body == "LOGOFF" || body == "LOGOFF ACCEPTED") disconnect();
}

void CpdlcSession::onSendResult(const std::string& recipient, const CpdlcPacket& packet, bool success) {
    if (packet.body == "REQUEST LOGON" && recipient == pendingAtsu_ && !success &&
        (state_ == CpdlcSessionState::LogonPending || state_ == CpdlcSessionState::HandoverPending)) {
        pendingAtsu_.clear();
        state_ = currentAtsu_.empty() ? CpdlcSessionState::Offline : CpdlcSessionState::Active;
        lastError_ = "LOGON SEND FAILED";
    } else if (packet.body == "LOGOFF" && recipient == currentAtsu_ &&
               state_ == CpdlcSessionState::LoggingOff) {
        if (success) disconnect();
        else state_ = CpdlcSessionState::Active;
    }
}

void CpdlcSession::disconnect() {
    state_ = CpdlcSessionState::Offline;
    currentAtsu_.clear();
    pendingAtsu_.clear();
}

bool CpdlcSession::updateCallsign(std::string callsign) {
    callsign = uppercaseAscii(trim(callsign));
    if (callsign == callsign_) return false;
    callsign_ = std::move(callsign);
    disconnect();
    return true;
}

std::string buildDclRequest(const DclRequest& request) {
    const auto callsign = requireUpperToken(request.callsign, "callsign");
    const auto aircraft = requireUpperToken(request.aircraftType, "aircraft type");
    const auto destination = requireUpperToken(request.destination, "destination");
    const auto departure = requireUpperToken(request.departure, "departure");
    if (!validCallsign(callsign) || !validIcao(destination) || !validIcao(departure))
        throw std::invalid_argument("invalid DCL flight fields");
    std::string result = "REQUEST PREDEP CLEARANCE " + callsign + " " + aircraft + " TO " +
                         destination + " AT " + departure;
    if (!trim(request.stand).empty()) result += " STAND " + requireUpperToken(request.stand, "stand");
    if (!trim(request.atis).empty()) result += " ATIS " + requireUpperToken(request.atis, "ATIS");
    if (!trim(request.remarks).empty()) result += " " + requireAviationText(request.remarks, "remarks");
    return result;
}

std::string buildAltitudeRequest(std::string_view level) {
    auto value = uppercaseAscii(trim(level));
    if (startsWith(value, "FL")) value.erase(0, 2);
    if (!validFlightLevel(value)) throw std::invalid_argument("invalid flight level");
    return "REQUEST CLIMB TO FL" + value;
}

std::string buildDescentRequest(std::string_view level) {
    auto value = uppercaseAscii(trim(level));
    if (startsWith(value, "FL")) value.erase(0, 2);
    if (!validFlightLevel(value)) throw std::invalid_argument("invalid flight level");
    return "REQUEST DESCENT TO FL" + value;
}

std::string buildDirectRequest(std::string_view waypoint) {
    return "REQUEST DIRECT TO " + requireUpperToken(waypoint, "waypoint");
}

std::string buildSpeedRequest(std::string_view speedOrMach) {
    auto value = uppercaseAscii(trim(speedOrMach));
    if (value.empty()) throw std::invalid_argument("speed is empty");
    std::string mach;
    if (startsWith(value, "MACH ")) mach = trim(std::string_view(value).substr(5));
    else if (value.front() == 'M') mach = trim(std::string_view(value).substr(1));
    else if (value.front() == '.') mach = value;
    if (!mach.empty()) {
        if (mach.size() != 3 || mach[0] != '.' || !std::isdigit(mach[1]) ||
            !std::isdigit(mach[2]) || std::stoi(mach.substr(1)) < 30)
            throw std::invalid_argument("invalid Mach number");
        return "REQUEST MACH " + mach;
    }
    if (startsWith(value, "SPEED ")) value = trim(std::string_view(value).substr(6));
    if (value.size() > 3 && value.substr(value.size() - 3) == " KT")
        value = trim(std::string_view(value).substr(0, value.size() - 3));
    else if (value.size() > 6 && value.substr(value.size() - 6) == " KNOTS")
        value = trim(std::string_view(value).substr(0, value.size() - 6));
    if (value.size() != 3 || !std::all_of(value.begin(), value.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        }) || std::stoi(value) < 100 || std::stoi(value) > 600)
        throw std::invalid_argument("invalid indicated airspeed");
    return "REQUEST SPEED " + value + " KT";
}

std::string buildWhenExpectedRequest(std::string_view target) {
    const auto value = requireAviationText(target, "target");
    return "WHEN CAN WE EXPECT " + value;
}

std::string buildPositionReport(const PositionReport& report) {
    auto level = uppercaseAscii(trim(report.flightLevel));
    if (startsWith(level, "FL")) level.erase(0, 2);
    if (!validFlightLevel(level)) throw std::invalid_argument("invalid flight level");
    return "POSITION REPORT " + requireUpperToken(report.callsign, "callsign") + " " +
           requireUpperToken(report.currentFix, "current fix") + " AT " +
           requireUpperToken(report.currentTime, "current time") + " FL" + level + " ESTIMATING " +
           requireUpperToken(report.nextFix, "next fix") + " AT " +
           requireUpperToken(report.nextEta, "next ETA") + " NEXT " +
           requireUpperToken(report.followingFix, "following fix");
}

std::string buildOceanicClearance(std::string_view callsign, std::string_view aircraftType,
                                  std::string_view entryPoint, std::string_view entryTime,
                                  std::string_view flightLevel, std::string_view mach,
                                  std::string_view route) {
    auto level = uppercaseAscii(trim(flightLevel));
    if (startsWith(level, "FL")) level.erase(0, 2);
    auto speed = uppercaseAscii(trim(mach));
    if (!startsWith(speed, "M")) speed = "M" + speed;
    const auto routeText = requireAviationText(route, "route");
    return "REQUEST OCEANIC CLEARANCE " + requireUpperToken(callsign, "callsign") + " " +
           requireUpperToken(aircraftType, "aircraft type") + " ENTRY " +
           requireUpperToken(entryPoint, "entry point") + " AT " +
           requireUpperToken(entryTime, "entry time") + " FL" +
           requireUpperToken(level, "flight level") + " " + speed + " VIA " +
           routeText;
}

std::string buildInfoRequest(std::string_view product, std::string_view icao) {
    static const std::set<std::string> products{"METAR", "TAF", "SHORTTAF", "VATATIS",
                                                 "IVAOATIS", "PEATIS"};
    auto kind = uppercaseAscii(trim(product));
    if (products.count(kind) == 0) throw std::invalid_argument("unknown information product");
    auto airport = uppercaseAscii(trim(icao));
    if (!validIcao(airport)) throw std::invalid_argument("invalid ICAO code");
    std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return kind + " " + airport;
}

bool validCallsign(std::string_view callsign) {
    const auto value = uppercaseAscii(trim(callsign));
    return value.size() >= 2 && value.size() <= 10 &&
           std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
           });
}

bool validIcao(std::string_view icao) {
    const auto value = uppercaseAscii(trim(icao));
    return value.size() == 4 && std::all_of(value.begin(), value.end(), [](char c) {
               return c >= 'A' && c <= 'Z';
           });
}

bool validFlightLevel(std::string_view level) {
    auto value = uppercaseAscii(trim(level));
    if (startsWith(value, "FL")) value.erase(0, 2);
    if (value.size() != 3 || !std::all_of(value.begin(), value.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        })) return false;
    const int numeric = std::stoi(value);
    return numeric >= 10 && numeric <= 600;
}

void PollScheduler::reset() {
    activated_ = false;
    failureCount_ = 0;
    regularPoll_.reset();
}

void PollScheduler::recordRealRequest(TimePoint now) {
    // Additional sends must not postpone receipt indefinitely or bypass a failure backoff.
    if (activated_) return;
    activated_ = true;
    failureCount_ = 0;
    regularPoll_ = now + std::chrono::seconds(30);
}

bool PollScheduler::pollDue(TimePoint now) const {
    if (!activated_) return false;
    return regularPoll_ && now >= *regularPoll_;
}

void PollScheduler::recordPollResult(TimePoint now, bool success) {
    if (!activated_) return;
    if (success) {
        failureCount_ = 0;
        regularPoll_ = now + std::chrono::seconds(30);
    } else {
        ++failureCount_;
        const auto backoff =
            std::min<unsigned>(300, 60u << std::min<unsigned>(failureCount_ - 1, 3));
        regularPoll_ = now + std::chrono::seconds(backoff);
    }
}

std::optional<PollScheduler::TimePoint> PollScheduler::nextPoll() const {
    return regularPoll_;
}

AdscManager::AdscManager(bool userEnabled) { contract_.enabled = userEnabled; }

void AdscManager::setUserEnabled(bool enabled) {
    contract_.enabled = enabled;
    if (!enabled) cancel();
}

bool AdscManager::acceptPeriodic(std::string callsign, unsigned intervalSeconds,
                                 TimePoint now, std::string* rejection) {
    if (!contract_.enabled) {
        if (rejection) *rejection = "ADS-C DISABLED";
        return false;
    }
    if (!validCallsign(callsign) || intervalSeconds < 60) {
        if (rejection) *rejection = intervalSeconds < 60 ? "UNABLE INTERVAL TOO SHORT"
                                                        : "UNABLE INVALID CALLSIGN";
        return false;
    }
    contract_.active = true;
    contract_.callsign = uppercaseAscii(trim(callsign));
    contract_.intervalSeconds = intervalSeconds;
    contract_.nextReport = now + std::chrono::seconds(intervalSeconds);
    return true;
}

bool AdscManager::parseContractRequest(std::string_view callsign, std::string_view body,
                                       TimePoint now, std::string* response) {
    const auto request = uppercaseAscii(trim(body));
    if (request == "CANCEL") {
        cancel();
        if (response) *response = "CANCELLED";
        return true;
    }
    constexpr std::string_view prefix = "REQUEST PERIODIC ";
    if (!startsWith(request, prefix)) {
        if (response) *response = "UNABLE EVENT CONTRACTS NOT SUPPORTED";
        return false;
    }
    const auto secondsText = trim(std::string_view(request).substr(prefix.size()));
    if (secondsText.empty() || !std::all_of(secondsText.begin(), secondsText.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        })) {
        if (response) *response = "UNABLE INVALID PERIOD";
        return false;
    }
    try {
        const auto seconds = static_cast<unsigned>(std::stoul(secondsText));
        std::string rejection;
        if (!acceptPeriodic(std::string(callsign), seconds, now, &rejection)) {
            if (response) *response = rejection;
            return false;
        }
    } catch (...) {
        if (response) *response = "UNABLE INVALID PERIOD";
        return false;
    }
    if (response) *response = "ACCEPTED PERIODIC " + std::to_string(contract_.intervalSeconds);
    return true;
}

void AdscManager::cancel() {
    contract_.active = false;
    contract_.intervalSeconds = 0;
    contract_.callsign.clear();
}

void AdscManager::onPluginDisabled() { cancel(); }

void AdscManager::onCallsignChanged(std::string_view callsign) {
    if (contract_.active && uppercaseAscii(trim(callsign)) != contract_.callsign) cancel();
}

bool AdscManager::reportDue(TimePoint now) const {
    return contract_.enabled && contract_.active && now >= contract_.nextReport;
}

void AdscManager::reportSent(TimePoint now) {
    if (contract_.active)
        contract_.nextReport = now + std::chrono::seconds(contract_.intervalSeconds);
}

std::string buildAdscReport(std::string_view callsign, std::string_view utcDayHourMinute,
                            const AircraftSnapshot& snapshot) {
    const auto altitude = static_cast<long>(std::lround(snapshot.mslAltitudeFeet / 100.0));
    return "REPORT " + requireUpperToken(callsign, "callsign") + " " +
           requireUpperToken(utcDayHourMinute, "time") + " " +
           formatCoordinate(snapshot.latitude, true) + " " +
           formatCoordinate(snapshot.longitude, false) + " " + std::to_string(altitude);
}

FlightPlanSnapshot mergeFlightPlans(const FlightPlanSnapshot& manual,
                                    const std::optional<FlightPlanSnapshot>& onlineVatsim,
                                    const std::optional<FlightPlanSnapshot>& prefileVatsim,
                                    const std::optional<FlightPlanSnapshot>& simbrief) {
    FlightPlanSnapshot result;
    const auto vatsim = onlineVatsim ? onlineVatsim : prefileVatsim;
    if (vatsim) result = *vatsim;
    if (simbrief) {
        overlayField(result.callsign, simbrief->callsign);
        overlayField(result.aircraftType, simbrief->aircraftType);
        overlayField(result.departure, simbrief->departure);
        overlayField(result.destination, simbrief->destination);
        overlayField(result.route, simbrief->route);
        if (result.waypoints.empty()) result.waypoints = simbrief->waypoints;
    }
    auto manualField = [&](const char* key, std::string& target, const std::string& value) {
        if (manual.manualFields.count(key) != 0) target = value;
    };
    manualField("callsign", result.callsign, manual.callsign);
    manualField("aircraftType", result.aircraftType, manual.aircraftType);
    manualField("departure", result.departure, manual.departure);
    manualField("destination", result.destination, manual.destination);
    manualField("route", result.route, manual.route);
    if (manual.manualFields.count("waypoints") != 0) result.waypoints = manual.waypoints;
    result.manualFields = manual.manualFields;
    return result;
}

std::vector<std::int16_t> buildMessageAlertPcm() {
    constexpr int toneSamples = messageAlertSampleRate * 350 / 1000;
    constexpr int gapSamples = messageAlertSampleRate * 150 / 1000;
    constexpr int fadeSamples = messageAlertSampleRate / 100;
    std::vector<std::int16_t> samples(3 * toneSamples + 2 * gapSamples, 0);
    for (int beep = 0; beep < 3; ++beep) {
        for (int index = 0; index < toneSamples; ++index) {
            // Fade both edges over 10 ms to avoid clicks at each pulse boundary.
            const double envelope = std::min(1.0, index / double(fadeSamples)) *
                                    std::min(1.0, (toneSamples - 1 - index) / double(fadeSamples));
            samples[static_cast<std::size_t>(beep * (toneSamples + gapSamples) + index)] =
                static_cast<std::int16_t>(std::sin(2.0 * 3.14159265358979323846 *
                    880.0 * index / messageAlertSampleRate) * envelope * 9000.0);
        }
    }
    return samples;
}

const std::vector<std::string>& translationKeys() {
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> result;
        for (const auto& item : dictionaries().at("en")) result.push_back(item.first);
        return result;
    }();
    return keys;
}

std::string translate(std::string_view language, std::string_view key) {
    const auto lang = dictionaries().find(std::string(language));
    const auto& dictionary = lang == dictionaries().end() ? dictionaries().at("en") : lang->second;
    const auto item = dictionary.find(std::string(key));
    return item == dictionary.end() ? std::string(key) : item->second;
}

bool translationsComplete() {
    const auto& english = dictionaries().at("en");
    for (const auto& language : dictionaries()) {
        if (language.second.size() != english.size()) return false;
        for (const auto& item : english)
            if (language.second.count(item.first) == 0) return false;
    }
    return true;
}

}  // namespace hoppie
