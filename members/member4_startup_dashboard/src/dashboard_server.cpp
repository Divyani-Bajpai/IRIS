#define WIN32_LEAN_AND_MEAN
#include "iris/dashboard_server.h"
#include "deviation_calculator.h"
#include "rule_engine.h"

#include <sqlite3.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace iris {
namespace {

std::string jsonEscape(const unsigned char* input) {
    if (input == nullptr) return {};
    std::ostringstream out;
    for (const unsigned char* p = input; *p != 0; ++p) {
        switch (*p) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (*p < 0x20) {
                const char* hex = "0123456789abcdef";
                out << "\\u00" << hex[*p >> 4] << hex[*p & 0x0f];
            } else {
                out << static_cast<char>(*p);
            }
        }
    }
    return out.str();
}

std::string columnText(sqlite3_stmt* statement, int column) {
    return jsonEscape(sqlite3_column_text(statement, column));
}

// DEMO-MODE-START: builds a sample diagnosis from the real rule engine so the
// report can be shown without putting real load on the PC.
std::string demoDiagnosisJson(const RuleResult& rule) {
    const auto escape = [](const std::string& text) {
        return jsonEscape(reinterpret_cast<const unsigned char*>(text.c_str()));
    };
    std::ostringstream out;
    out << "{\"rule\":\"" << escape(rule.ruleName)
        << "\",\"diagnosis\":\"" << escape(rule.diagnosis)
        << "\",\"reason\":\"" << escape(rule.reason)
        << "\",\"recommendation\":\"" << escape(rule.recommendationText)
        << "\",\"suggestions\":[";
    for (std::size_t i = 0; i < rule.suggestions.size(); ++i) {
        if (i > 0) out << ',';
        out << '"' << escape(rule.suggestions[i]) << '"';
    }
    out << "]}";
    return out.str();
}
// DEMO-MODE-END

std::string dashboardHtmlPath() {
    // CMake copies the active page beside the executable in the build folder.
    // Resolve that location first so launching IRIS from another working
    // directory does not break the dashboard.
    char executablePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        std::string path(executablePath, length);
        const std::size_t separator = path.find_last_of("\\/");
        if (separator != std::string::npos) {
            path.resize(separator + 1);
            path += "index.html";
            std::ifstream executableDirectoryPage(path, std::ios::binary);
            if (executableDirectoryPage) return path;
        }
    }

    // Keep the source-root launch workflow working as a fallback.
    for (const char* candidate : {"index.html", "build/index.html"}) {
        std::ifstream page(candidate, std::ios::binary);
        if (page) return candidate;
    }
    return "index.html";
}

bool querySnapshot(const std::string& databasePath, const char* sessionState,
                   long long sessionStartedEpoch,
                   std::string& json, std::string& error) {
    sqlite3* db = nullptr;
    const int openStatus = sqlite3_open_v2(databasePath.c_str(), &db,
        SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (openStatus != SQLITE_OK) {
        error = db ? sqlite3_errmsg(db) : "could not open database";
        if (db) sqlite3_close(db);
        return false;
    }
    sqlite3_busy_timeout(db, 1000);
    std::ostringstream output;
    output << "{\"metrics\":[";
    bool first = true;
    std::map<std::string, double> currentValues;
    std::map<std::string, std::pair<double, double>> baselineValues;
    const char* metricSql =
        "SELECT m.metric_name,m.value,m.timestamp,b.average_value,"
        "b.standard_deviation,b.source FROM live_metrics m "
        "LEFT JOIN baselines b ON b.metric_name=m.metric_name "
        "WHERE m.process_name IS NULL AND (?=0 OR datetime(m.timestamp)>=datetime(?,'unixepoch')) "
        "AND m.id=(SELECT MAX(x.id) "
        "FROM live_metrics x WHERE x.metric_name=m.metric_name "
        "AND x.process_name IS NULL AND (?=0 OR datetime(x.timestamp)>=datetime(?,'unixepoch'))) "
        "ORDER BY m.metric_name;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, metricSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db); sqlite3_close(db); return false;
    }
    for (int i = 1; i <= 4; ++i) sqlite3_bind_int64(statement, i, sessionStartedEpoch);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* metricText = sqlite3_column_text(statement, 0);
        if (metricText != nullptr) {
            const std::string metricName(reinterpret_cast<const char*>(metricText));
            currentValues[metricName] = sqlite3_column_double(statement, 1);
            if (sqlite3_column_type(statement, 3) != SQLITE_NULL) {
                baselineValues[metricName] = {
                    sqlite3_column_double(statement, 3),
                    sqlite3_column_double(statement, 4)};
            }
        }
        if (!first) output << ',';
        first = false;
        output << "{\"name\":\"" << columnText(statement, 0)
               << "\",\"value\":" << sqlite3_column_double(statement, 1)
               << ",\"timestamp\":\"" << columnText(statement, 2) << '"';
        if (sqlite3_column_type(statement, 3) != SQLITE_NULL) {
            output << ",\"mean\":" << sqlite3_column_double(statement, 3)
                   << ",\"stddev\":" << sqlite3_column_double(statement, 4)
                   << ",\"source\":\"" << columnText(statement, 5) << '"';
        } else {
            output << ",\"mean\":null,\"stddev\":null,\"source\":null";
        }
        output << '}';
    }
    sqlite3_finalize(statement);

    output << "],\"recent_readings\":[";
    const char* recentSql =
        "SELECT timestamp,metric_name,value,process_name FROM live_metrics "
        "WHERE (?=0 OR datetime(timestamp)>=datetime(?,'unixepoch')) "
        "ORDER BY id DESC;";
    if (sqlite3_prepare_v2(db, recentSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db); sqlite3_close(db); return false;
    }
    sqlite3_bind_int64(statement, 1, sessionStartedEpoch);
    sqlite3_bind_int64(statement, 2, sessionStartedEpoch);
    first = true;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (!first) output << ',';
        first = false;
        output << "{\"timestamp\":\"" << columnText(statement, 0)
               << "\",\"name\":\"" << columnText(statement, 1)
               << "\",\"value\":" << sqlite3_column_double(statement, 2)
               << ",\"process\":";
        if (sqlite3_column_type(statement, 3) == SQLITE_NULL) output << "null";
        else output << '"' << columnText(statement, 3) << '"';
        output << '}';
    }
    sqlite3_finalize(statement);

    output << "],\"sample_count\":";
    const char* sampleCountSql =
        "SELECT COUNT(*) FROM live_metrics "
        "WHERE metric_name='cpu_percent' AND process_name IS NULL "
        "AND (?=0 OR datetime(timestamp)>=datetime(?,'unixepoch'));";
    if (sqlite3_prepare_v2(db, sampleCountSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db); sqlite3_close(db); return false;
    }
    sqlite3_bind_int64(statement, 1, sessionStartedEpoch);
    sqlite3_bind_int64(statement, 2, sessionStartedEpoch);
    output << (sqlite3_step(statement) == SQLITE_ROW
        ? sqlite3_column_int64(statement, 0) : 0);
    sqlite3_finalize(statement);

    output << ",\"history\":[";
    const char* historySql =
        "SELECT timestamp,metric_name,value FROM live_metrics "
        "WHERE process_name IS NULL AND metric_name IN "
        "('cpu_percent','percent_disk_time') AND "
        "datetime(timestamp)>=datetime('now','-30 minutes') ORDER BY id;";
    if (sqlite3_prepare_v2(db, historySql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db); sqlite3_close(db); return false;
    }
    first = true;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (!first) output << ',';
        first = false;
        output << "{\"timestamp\":\"" << columnText(statement, 0)
               << "\",\"name\":\"" << columnText(statement, 1)
               << "\",\"value\":" << sqlite3_column_double(statement, 2) << '}';
    }
    sqlite3_finalize(statement);

    output << "],\"processes\":[";
    struct ProcessValues {
        double cpu = 0.0;
        double memory = 0.0;
        double diskRead = 0.0;
        double diskWrite = 0.0;
        bool hasCpu = false;
        bool hasMemory = false;
        bool hasDiskRead = false;
        bool hasDiskWrite = false;
    };
    std::map<std::string, ProcessValues> processes;
    const char* processSql =
        "SELECT metric_name,process_name,value FROM live_metrics m "
        "WHERE process_name IS NOT NULL AND metric_name IN "
        "('process_cpu_percent','process_memory_mb','process_disk_read_mb_per_sec',"
        "'process_disk_write_mb_per_sec') AND datetime(m.timestamp)=(SELECT MAX(datetime(y.timestamp)) "
        "FROM live_metrics y WHERE y.process_name IS NOT NULL "
        "AND (?=0 OR datetime(y.timestamp)>=datetime(?,'unixepoch'))) "
        "AND id=(SELECT MAX(x.id) "
        "FROM live_metrics x WHERE x.process_name=m.process_name "
        "AND x.metric_name=m.metric_name AND x.timestamp=m.timestamp);";
    if (sqlite3_prepare_v2(db, processSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db); sqlite3_close(db); return false;
    }
    sqlite3_bind_int64(statement, 1, sessionStartedEpoch);
    sqlite3_bind_int64(statement, 2, sessionStartedEpoch);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* metricText = sqlite3_column_text(statement, 0);
        const auto* nameText = sqlite3_column_text(statement, 1);
        if (!metricText || !nameText) continue;
        const std::string metric(reinterpret_cast<const char*>(metricText));
        const std::string name(reinterpret_cast<const char*>(nameText));
        auto& values = processes[name];
        if (metric == "process_cpu_percent") {
            values.cpu = sqlite3_column_double(statement, 2);
            values.hasCpu = true;
        } else if (metric == "process_memory_mb") {
            values.memory = sqlite3_column_double(statement, 2);
            values.hasMemory = true;
        } else if (metric == "process_disk_read_mb_per_sec") {
            values.diskRead = sqlite3_column_double(statement, 2);
            values.hasDiskRead = true;
        } else {
            values.diskWrite = sqlite3_column_double(statement, 2);
            values.hasDiskWrite = true;
        }
    }
    sqlite3_finalize(statement);
    first = true;
    for (const auto& process : processes) {
        if (!first) output << ',';
        first = false;
        output << "{\"name\":\""
               << jsonEscape(reinterpret_cast<const unsigned char*>(process.first.c_str()))
               << "\",\"cpu_percent\":";
        if (process.second.hasCpu) output << process.second.cpu;
        else output << "null";
        output << ",\"memory_mb\":";
        if (process.second.hasMemory) output << process.second.memory;
        else output << "null";
        output << ",\"disk_read_mb_sec\":";
        if (process.second.hasDiskRead) output << process.second.diskRead;
        else output << "null";
        output << ",\"disk_write_mb_sec\":";
        if (process.second.hasDiskWrite) output << process.second.diskWrite;
        else output << "null";
        output << '}';
    }

    output << "],\"startup_apps\":[";
    const char* startupSql =
        "SELECT app_name,registry_location FROM startup_apps "
        "WHERE timestamp=(SELECT MAX(timestamp) FROM startup_apps) ORDER BY app_name;";
    if (sqlite3_prepare_v2(db, startupSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db); sqlite3_close(db); return false;
    }
    first = true;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (!first) output << ',';
        first = false;
        output << "{\"name\":\"" << columnText(statement, 0)
               << "\",\"location\":\"" << columnText(statement, 1) << "\"}";
    }
    sqlite3_finalize(statement);

    output << "],\"diagnosis\":";
    const char* eventSql =
        "SELECT e.timestamp,e.rule_matched,e.metric_values,r.recommendation_text "
        "FROM diagnostic_events e LEFT JOIN diagnostic_rules r "
        "ON r.rule_name=e.rule_matched ORDER BY e.id DESC LIMIT 1;";
    if (sqlite3_prepare_v2(db, eventSql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db); sqlite3_close(db); return false;
    }
    if (sqlite3_step(statement) == SQLITE_ROW) {
        output << "{\"timestamp\":\"" << columnText(statement, 0)
               << "\",\"rule\":\"" << columnText(statement, 1)
               << "\",\"evidence\":\"" << columnText(statement, 2)
               << "\",\"recommendation\":\"" << columnText(statement, 3) << "\"}";
    } else {
        output << "null";
    }
    sqlite3_finalize(statement);

    const double missing = std::numeric_limits<double>::quiet_NaN();
    MetricScores scores{missing, missing, missing, missing, missing};
    const auto zScore = [&](const char* metric, double& target) {
        const auto value = currentValues.find(metric);
        const auto baseline = baselineValues.find(metric);
        if (value != currentValues.end() && baseline != baselineValues.end()) {
            deviationCalculation(value->second, baseline->second.first,
                                 baseline->second.second, target);
        }
    };
    zScore("page_faults_per_sec", scores.pageFaultZ);
    zScore("available_ram_mb", scores.availableRamZ);
    zScore("cpu_percent", scores.cpuZ);
    zScore("disk_queue_length", scores.diskQueueZ);
    zScore("percent_disk_time", scores.diskTimeZ);
    const RuleResult currentRule = evaluateRules(scores);
    output << ",\"current_diagnosis\":";
    if (currentRule.matched) {
        output << "{\"rule\":\"" << jsonEscape(
            reinterpret_cast<const unsigned char*>(currentRule.ruleName.c_str()))
               << "\",\"diagnosis\":\"" << jsonEscape(
            reinterpret_cast<const unsigned char*>(currentRule.diagnosis.c_str()))
               << "\",\"reason\":\"" << jsonEscape(
            reinterpret_cast<const unsigned char*>(currentRule.reason.c_str()))
               << "\",\"recommendation\":\"" << jsonEscape(
            reinterpret_cast<const unsigned char*>(currentRule.recommendationText.c_str()))
               << "\",\"suggestions\":[";
        for (std::size_t i = 0; i < currentRule.suggestions.size(); ++i) {
            if (i > 0) output << ',';
            output << '"' << jsonEscape(reinterpret_cast<const unsigned char*>(
                currentRule.suggestions[i].c_str())) << '"';
        }
        output << "]}";
    } else {
        output << "null";
    }

    output << ",\"history_days\":";
    if (sqlite3_prepare_v2(db,
            "SELECT CAST(MAX(0,julianday('now')-julianday(value)) AS INTEGER) "
            "FROM iris_metadata WHERE key='first_metric_timestamp';",
            -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db); sqlite3_close(db); return false;
    }
    output << (sqlite3_step(statement) == SQLITE_ROW
        ? sqlite3_column_int(statement, 0) : 0);
    sqlite3_finalize(statement);
    output << ",\"baseline_count\":";
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM baselines;", -1,
                           &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db); sqlite3_close(db); return false;
    }
    output << (sqlite3_step(statement) == SQLITE_ROW
        ? sqlite3_column_int(statement, 0) : 0);
    sqlite3_finalize(statement);
    output << ",\"session_state\":\"" << sessionState << "\"}";
    sqlite3_close(db);
    json = output.str();
    return true;
}

void sendResponse(SOCKET client, int status, const char* reason,
                  const char* contentType, const std::string& body) {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
            << "Content-Type: " << contentType << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "Connection: close\r\n\r\n";
    const std::string response = headers.str() + body;
    const char* data = response.data();
    std::size_t remaining = response.size();
    while (remaining > 0) {
        const int sent = send(client, data,
            static_cast<int>(std::min<std::size_t>(remaining, 64 * 1024)), 0);
        if (sent <= 0) break;
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
}

}  // namespace

DashboardServer::DashboardServer(std::string databasePath, unsigned short port)
    : databasePath_(std::move(databasePath)), port_(port) {}

DashboardServer::~DashboardServer() { stop(); }

void DashboardServer::setSessionState(DashboardSessionState state) {
    if (state == DashboardSessionState::Starting ||
        state == DashboardSessionState::Collecting) {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        sessionStartedEpoch_.store(std::chrono::duration_cast<std::chrono::seconds>(now).count());
    }
    sessionState_.store(state);
}

bool DashboardServer::consumeCollectionRequest() {
    return collectionRequested_.exchange(false);
}

bool DashboardServer::start(std::string& error) {
    if (running_) return true;
    WSADATA data{};
    const int startup = WSAStartup(MAKEWORD(2, 2), &data);
    if (startup != 0) {
        error = "WSAStartup failed: " + std::to_string(startup);
        return false;
    }
    winsockStarted_ = true;
    const unsigned short preferredPort = port_;
    bool bound = false;
    for (unsigned int offset = 0; offset < 11; ++offset) {
        port_ = static_cast<unsigned short>(preferredPort + offset);
        listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket_ == INVALID_SOCKET) {
            error = "Could not create dashboard socket: "
                  + std::to_string(WSAGetLastError());
            stop(); return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port_);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)) == 0) {
            bound = true;
            break;
        }
        const int bindError = WSAGetLastError();
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        if (bindError != WSAEADDRINUSE) {
            error = "Could not bind local dashboard socket (error "
                  + std::to_string(bindError) + ")";
            stop(); return false;
        }
    }
    if (!bound) {
        error = "Dashboard ports 8765-8775 are already in use";
        stop(); return false;
    }
    if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        error = "Could not listen on dashboard port " + std::to_string(port_)
              + " (socket error " + std::to_string(WSAGetLastError()) + ")";
        stop(); return false;
    }
    running_ = true;
    worker_ = std::thread(&DashboardServer::serve, this);
    return true;
}

void DashboardServer::stop() {
    running_ = false;
    // Let the server thread leave its timed select before closing the socket.
    // Closing a socket from another thread while it is blocked in accept() can
    // leave accept() blocked indefinitely on Windows.
    if (worker_.joinable()) worker_.join();
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    if (winsockStarted_) {
        WSACleanup();
        winsockStarted_ = false;
    }
}

void DashboardServer::serve() {
    while (running_) {
        const SOCKET listeningSocket = listenSocket_;
        if (listeningSocket == INVALID_SOCKET) break;

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listeningSocket, &readSet);
        timeval waitForConnection{0, 250000};
        const int ready = select(0, &readSet, nullptr, nullptr,
                                 &waitForConnection);
        if (!running_) break;
        if (ready == 0) continue;
        if (ready == SOCKET_ERROR) {
            if (running_) Sleep(25);
            continue;
        }

        SOCKET client = accept(listeningSocket, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (running_) Sleep(25);
            continue;
        }
        const DWORD receiveTimeoutMs = 1000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&receiveTimeoutMs),
                   sizeof(receiveTimeoutMs));
        handleClient(client);
        shutdown(client, SD_SEND);
        closesocket(client);
    }
}

void DashboardServer::handleClient(SOCKET client) {
    std::string request;
    char buffer[2048];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) return;
        request.append(buffer, static_cast<std::size_t>(received));
    }
    const std::size_t end = request.find("\r\n");
    const std::string firstLine = request.substr(0, end);
    if (firstLine.rfind("POST /api/start-check ", 0) == 0) {
        const DashboardSessionState currentState = sessionState_.load();
        if (currentState == DashboardSessionState::Starting ||
            currentState == DashboardSessionState::Collecting ||
            collectionRequested_.load()) {
            sendResponse(client, 409, "Conflict", "application/json; charset=utf-8",
                         "{\"error\":\"A check is already running.\"}");
        } else {
            setSessionState(DashboardSessionState::Starting);
            collectionRequested_.store(true);
            sendResponse(client, 202, "Accepted", "application/json; charset=utf-8",
                         "{\"started\":true}");
        }
        return;
    }
    if (firstLine.rfind("GET /api/snapshot ", 0) == 0) {
        std::string json, error;
        const DashboardSessionState state = sessionState_.load();
        const char* stateText = state == DashboardSessionState::Complete
            ? "complete"
            : (state == DashboardSessionState::Collecting ? "collecting"
                : (state == DashboardSessionState::Starting ? "starting" : "idle"));
        if (querySnapshot(databasePath_, stateText, sessionStartedEpoch_.load(), json, error)) {
            sendResponse(client, 200, "OK", "application/json; charset=utf-8", json);
        } else {
            sendResponse(client, 500, "Internal Server Error",
                "application/json; charset=utf-8",
                "{\"error\":\"" + jsonEscape(
                    reinterpret_cast<const unsigned char*>(error.c_str())) + "\"}");
        }
        return;
    }
    // DEMO-MODE-START
    if (firstLine.rfind("GET /api/demo-diagnosis?rule=", 0) == 0) {
        const bool memory = firstLine.rfind(
            "GET /api/demo-diagnosis?rule=memory_thrashing ", 0) == 0;
        const bool disk = firstLine.rfind(
            "GET /api/demo-diagnosis?rule=disk_io_contention ", 0) == 0;
        if (!memory && !disk) {
            sendResponse(client, 400, "Bad Request",
                         "application/json; charset=utf-8",
                         "{\"error\":\"Unknown demo rule.\"}");
            return;
        }
        // Fixed sample z-scores. The normal rule engine decides the match and
        // supplies the text, exactly as it does for a real check.
        MetricScores scores{0.0, 0.0, 0.0, 0.0, 0.0};
        if (memory) {
            scores.pageFaultZ = 3.0;
            scores.availableRamZ = -3.0;
        } else {
            scores.diskQueueZ = 3.0;
            scores.diskTimeZ = 3.0;
        }
        const RuleResult sample = evaluateRules(scores);
        if (!sample.matched) {
            sendResponse(client, 500, "Internal Server Error",
                         "application/json; charset=utf-8",
                         "{\"error\":\"Demo diagnosis did not match a rule.\"}");
            return;
        }
        sendResponse(client, 200, "OK", "application/json; charset=utf-8",
                     demoDiagnosisJson(sample));
        return;
    }
    // DEMO-MODE-END
    if (firstLine.rfind("GET / ", 0) == 0 ||
        firstLine.rfind("GET /index.html ", 0) == 0) {
        std::ifstream file(dashboardHtmlPath(), std::ios::binary);
        if (!file) {
            sendResponse(client, 404, "Not Found", "text/plain; charset=utf-8",
                         "Could not find index.html beside IRIS or in the project folder. Rebuild the project.");
            return;
        }
        std::ostringstream html;
        html << file.rdbuf();
        sendResponse(client, 200, "OK", "text/html; charset=utf-8", html.str());
        return;
    }
    sendResponse(client, 404, "Not Found", "text/plain; charset=utf-8", "Not found");
}

}  // namespace iris
