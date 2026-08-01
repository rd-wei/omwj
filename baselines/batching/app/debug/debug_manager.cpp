#include "debug_manager.h"
#include "debug_util.h"
#include "debug_util.h"
#include "data_structures/entry.h"
#include "data_structures/data_structures.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>

// Initialize static member
std::unique_ptr<DebugManager> DebugManager::instance = nullptr;

DebugManager::DebugManager() 
    : session_active(false)
    , tables_dumped(0)
    , logs_written(0) {
    // Initialize with default configuration
    memset(&config, 0, sizeof(config));
    config.level = DEBUG_LEVEL_NONE;
    config.output_mode = DEBUG_OUTPUT_FILE;
    config.tables.format = DEBUG_FORMAT_CSV;
    strcpy(config.session.session_prefix, "debug");
    strcpy(config.session.output_dir, "./debug");
    config.session.auto_session = true;
    config.session.timestamp_files = true;
    config.session.create_subdirs = true;
    
    // Check for environment override
    loadConfigFromEnvironment();
}

DebugManager& DebugManager::getInstance() {
    if (!instance) {
        instance = std::unique_ptr<DebugManager>(new DebugManager());
    }
    return *instance;
}

void DebugManager::loadConfig(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        DEBUG_WARN("Failed to open config file: %s", config_file.c_str());
        return;
    }
    
    std::string line;
    std::string current_section;
    
    while (std::getline(file, line)) {
        // Remove comments
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty()) continue;
        
        // Check for section header
        if (line[0] == '[' && line[line.length() - 1] == ']') {
            current_section = line.substr(1, line.length() - 2);
            continue;
        }
        
        // Parse key=value pairs
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        // Trim key and value
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Apply settings based on section and key
        if (current_section == "global") {
            if (key == "level") {
                if (value == "NONE") config.level = DEBUG_LEVEL_NONE;
                else if (value == "ERROR") config.level = DEBUG_LEVEL_ERROR;
                else if (value == "WARN") config.level = DEBUG_LEVEL_WARN;
                else if (value == "INFO") config.level = DEBUG_LEVEL_INFO;
                else if (value == "DEBUG") config.level = DEBUG_LEVEL_DEBUG;
                else if (value == "TRACE") config.level = DEBUG_LEVEL_TRACE;
            } else if (key == "output") {
                if (value == "CONSOLE") config.output_mode = DEBUG_OUTPUT_CONSOLE;
                else if (value == "FILE") config.output_mode = DEBUG_OUTPUT_FILE;
                else if (value == "BOTH") config.output_mode = DEBUG_OUTPUT_BOTH;
            }
        } else if (current_section == "phases") {
            bool enabled = (value == "true" || value == "1");
            if (key == "bottom_up") config.phases.bottom_up = enabled;
            else if (key == "top_down") config.phases.top_down = enabled;
            else if (key == "distribute") config.phases.distribute = enabled;
            else if (key == "alignment") config.phases.alignment = enabled;
            else if (key == "oblivious_sort") config.phases.oblivious_sort = enabled;
            else if (key == "comparators") config.phases.comparators = enabled;
            else if (key == "window_ops") config.phases.window_ops = enabled;
            else if (key == "encryption") config.phases.encryption = enabled;
        } else if (current_section == "tables") {
            if (key == "enabled") config.tables.enabled = (value == "true" || value == "1");
            else if (key == "format") {
                if (value == "CSV") config.tables.format = DEBUG_FORMAT_CSV;
                else if (value == "JSON") config.tables.format = DEBUG_FORMAT_JSON;
                else if (value == "BINARY") config.tables.format = DEBUG_FORMAT_BINARY;
            } else if (key == "max_rows") {
                config.tables.max_rows = static_cast<uint32_t>(std::stoul(value));
            } else if (key.find("stages.") == 0) {
                std::string stage = key.substr(7);
                bool enabled = (value == "true" || value == "1");
                if (stage == "inputs") config.tables.stages.inputs = enabled;
                else if (stage == "after_sort") config.tables.stages.after_sort = enabled;
                else if (stage == "after_cumsum") config.tables.stages.after_cumsum = enabled;
                else if (stage == "after_interval") config.tables.stages.after_interval = enabled;
                else if (stage == "after_pairwise") config.tables.stages.after_pairwise = enabled;
                else if (stage == "after_truncate") config.tables.stages.after_truncate = enabled;
                else if (stage == "after_expand") config.tables.stages.after_expand = enabled;
                else if (stage == "outputs") config.tables.stages.outputs = enabled;
            }
        } else if (current_section == "performance") {
            bool enabled = (value == "true" || value == "1");
            if (key == "enabled") config.perf.enabled = enabled;
            else if (key == "per_phase") config.perf.per_phase = enabled;
            else if (key == "per_operation") config.perf.per_operation = enabled;
            else if (key == "memory_usage") config.perf.memory_usage = enabled;
            else if (key == "enclave_transitions") config.perf.enclave_transitions = enabled;
        } else if (current_section == "session") {
            if (key == "auto_session") config.session.auto_session = (value == "true" || value == "1");
            else if (key == "timestamp_files") config.session.timestamp_files = (value == "true" || value == "1");
            else if (key == "create_subdirs") config.session.create_subdirs = (value == "true" || value == "1");
            else if (key == "session_prefix") {
                strncpy(config.session.session_prefix, value.c_str(), sizeof(config.session.session_prefix) - 1);
            } else if (key == "output_dir") {
                strncpy(config.session.output_dir, value.c_str(), sizeof(config.session.output_dir) - 1);
            }
        }
    }
    
    file.close();
    DEBUG_INFO("Loaded debug configuration from %s", config_file.c_str());
}

void DebugManager::loadConfigFromEnvironment() {
    // Check for environment variables
    const char* env_level = getenv("OMWJ_DEBUG_LEVEL");
    if (env_level) {
        if (strcmp(env_level, "NONE") == 0) config.level = DEBUG_LEVEL_NONE;
        else if (strcmp(env_level, "ERROR") == 0) config.level = DEBUG_LEVEL_ERROR;
        else if (strcmp(env_level, "WARN") == 0) config.level = DEBUG_LEVEL_WARN;
        else if (strcmp(env_level, "INFO") == 0) config.level = DEBUG_LEVEL_INFO;
        else if (strcmp(env_level, "DEBUG") == 0) config.level = DEBUG_LEVEL_DEBUG;
        else if (strcmp(env_level, "TRACE") == 0) config.level = DEBUG_LEVEL_TRACE;
    }
    
    const char* env_output = getenv("OMWJ_DEBUG_OUTPUT");
    if (env_output) {
        if (strcmp(env_output, "CONSOLE") == 0) config.output_mode = DEBUG_OUTPUT_CONSOLE;
        else if (strcmp(env_output, "FILE") == 0) config.output_mode = DEBUG_OUTPUT_FILE;
        else if (strcmp(env_output, "BOTH") == 0) config.output_mode = DEBUG_OUTPUT_BOTH;
    }
    
    const char* env_tables = getenv("OMWJ_DEBUG_TABLES");
    if (env_tables) {
        config.tables.enabled = (strcmp(env_tables, "1") == 0 || strcmp(env_tables, "true") == 0);
    }
}

void DebugManager::setConfig(const DebugConfig& cfg) {
    config = cfg;
}

void DebugManager::setDebugLevel(uint32_t level) {
    config.level = level;
}

void DebugManager::enablePhase(const std::string& phase, bool enable) {
    if (phase == "bottom_up") config.phases.bottom_up = enable;
    else if (phase == "top_down") config.phases.top_down = enable;
    else if (phase == "distribute") config.phases.distribute = enable;
    else if (phase == "alignment") config.phases.alignment = enable;
    else if (phase == "oblivious_sort") config.phases.oblivious_sort = enable;
    else if (phase == "comparators") config.phases.comparators = enable;
    else if (phase == "window_ops") config.phases.window_ops = enable;
    else if (phase == "encryption") config.phases.encryption = enable;
}

void DebugManager::enableTableDumps(bool enable) {
    config.tables.enabled = enable;
}

std::string DebugManager::generateSessionDir(const std::string& name) {
    std::stringstream ss;
    ss << config.session.output_dir << "/";
    
    if (config.session.timestamp_files) {
        auto now = std::chrono::system_clock::now();
        auto time_val = std::chrono::system_clock::to_time_t(now);
        struct tm* tm = localtime(&time_val);
        ss << std::setfill('0')
           << (1900 + tm->tm_year)
           << std::setw(2) << (1 + tm->tm_mon)
           << std::setw(2) << tm->tm_mday
           << "_"
           << std::setw(2) << tm->tm_hour
           << std::setw(2) << tm->tm_min
           << std::setw(2) << tm->tm_sec
           << "_";
    }
    
    ss << name;
    return ss.str();
}

void DebugManager::startSession(const std::string& name) {
    if (session_active) {
        endSession();
    }
    
    session_name = name;
    session_dir = generateSessionDir(name);
    
    // Create session directory if needed
    if (config.session.create_subdirs) {
        mkdir(session_dir.c_str(), 0755);
    }
    
    // Open log file
    if (config.output_mode == DEBUG_OUTPUT_FILE || config.output_mode == DEBUG_OUTPUT_BOTH) {
        std::string log_path = session_dir + "/debug.log";
        log_file.open(log_path, std::ios::out | std::ios::trunc);
        if (log_file.is_open()) {
            writeLogHeader();
        }
    }
    
    session_active = true;
    session_start = std::chrono::steady_clock::now();
    tables_dumped = 0;
    logs_written = 0;
    
    DEBUG_INFO("Debug session started: %s", name.c_str());
}

void DebugManager::endSession() {
    if (!session_active) return;
    
    auto session_end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(session_end - session_start);
    
    // Log performance summary if enabled
    if (config.perf.enabled) {
        logPerformanceSummary();
    }
    
    DEBUG_INFO("Debug session ended: %s (duration: %ld ms, tables: %zu, logs: %zu)",
               session_name.c_str(), duration.count(), tables_dumped, logs_written);
    
    if (log_file.is_open()) {
        writeLogFooter();
        log_file.close();
    }
    
    session_active = false;
    session_name.clear();
    session_dir.clear();
    phase_timers.clear();
    phase_durations.clear();
}

void DebugManager::writeLogHeader() {
    log_file << "=== Debug Session Started ===" << std::endl;
    log_file << "Session: " << session_name << std::endl;
    log_file << "Timestamp: " << getCurrentTimestamp() << std::endl;
    log_file << "Debug Level: " << config.level << std::endl;
    log_file << "=============================\n" << std::endl;
}

void DebugManager::writeLogFooter() {
    log_file << "\n=============================" << std::endl;
    log_file << "Session: " << session_name << std::endl;
    log_file << "Tables Dumped: " << tables_dumped << std::endl;
    log_file << "Logs Written: " << logs_written << std::endl;
    log_file << "=== Debug Session Ended ===" << std::endl;
}

bool DebugManager::shouldLog(uint32_t level) const {
    return level <= config.level;
}

bool DebugManager::shouldLog(uint32_t level, const std::string& module) const {
    if (level > config.level) return false;
    
    // Check module-specific enables
    if (module == "bottom_up") return config.phases.bottom_up;
    if (module == "top_down") return config.phases.top_down;
    if (module == "distribute") return config.phases.distribute;
    if (module == "alignment") return config.phases.alignment;
    if (module == "sort") return config.phases.oblivious_sort;
    if (module == "comparator") return config.phases.comparators;
    if (module == "window") return config.phases.window_ops;
    if (module == "encryption") return config.phases.encryption;
    
    // Default to true if module not recognized
    return true;
}

bool DebugManager::shouldDumpTable(const std::string& stage) const {
    if (!config.tables.enabled || !session_active) return false;
    
    if (stage == "input") return config.tables.stages.inputs;
    if (stage == "after_sort") return config.tables.stages.after_sort;
    if (stage == "after_cumsum") return config.tables.stages.after_cumsum;
    if (stage == "after_interval") return config.tables.stages.after_interval;
    if (stage == "after_pairwise") return config.tables.stages.after_pairwise;
    if (stage == "after_truncate") return config.tables.stages.after_truncate;
    if (stage == "after_expand") return config.tables.stages.after_expand;
    if (stage == "output") return config.tables.stages.outputs;
    
    // Default to true for unrecognized stages (backward compatibility)
    return true;
}

bool DebugManager::isPhaseEnabled(const std::string& phase) const {
    if (phase == "bottom_up") return config.phases.bottom_up;
    if (phase == "top_down") return config.phases.top_down;
    if (phase == "distribute") return config.phases.distribute;
    if (phase == "alignment") return config.phases.alignment;
    if (phase == "sort") return config.phases.oblivious_sort;
    if (phase == "comparator") return config.phases.comparators;
    if (phase == "window") return config.phases.window_ops;
    if (phase == "encryption") return config.phases.encryption;
    return false;
}

void DebugManager::log(uint32_t level, const char* file, int line, const char* fmt, ...) {
    if (!shouldLog(level)) return;
    
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    // Format the log message
    std::stringstream ss;
    
    const char* level_str = "UNKNOWN";
    switch (level) {
        case DEBUG_LEVEL_ERROR: level_str = "ERROR"; break;
        case DEBUG_LEVEL_WARN:  level_str = "WARN "; break;
        case DEBUG_LEVEL_INFO:  level_str = "INFO "; break;
        case DEBUG_LEVEL_DEBUG: level_str = "DEBUG"; break;
        case DEBUG_LEVEL_TRACE: level_str = "TRACE"; break;
    }
    
    // Extract filename without path
    const char* filename = strrchr(file, '/');
    if (filename) filename++;
    else filename = file;
    
    ss << "[" << level_str << "] "
       << filename << ":" << line << " - "
       << buffer;
    
    std::string message = ss.str();
    
    // Output to console if needed
    if (config.output_mode == DEBUG_OUTPUT_CONSOLE || config.output_mode == DEBUG_OUTPUT_BOTH) {
        printf("%s\n", message.c_str());
    }
    
    // Output to file if needed
    if ((config.output_mode == DEBUG_OUTPUT_FILE || config.output_mode == DEBUG_OUTPUT_BOTH) 
        && session_active && log_file.is_open()) {
        log_file << getCurrentTimestamp() << " " << message << std::endl;
        log_file.flush();
    }
    
    logs_written++;
}

void DebugManager::logToFile(const std::string& message) {
    if (session_active && log_file.is_open()) {
        log_file << message << std::endl;
        log_file.flush();
    }
}

void DebugManager::dumpTable(const Table& table, 
                             const std::string& stage,
                             const std::string& label,
                             uint32_t eid,
                             const std::vector<MetadataColumn>& columns,
                             bool include_attributes) {
    if (!shouldDumpTable(stage)) return;
    
    // Use existing debug_dump_table function for now
    // This will be refactored to use internal implementation later
    debug_dump_table(table, label.c_str(), 
                    (stage + "_" + label).c_str(), 
                    eid, columns, include_attributes);
    
    tables_dumped++;
}

void DebugManager::dumpTableWithMask(const Table& table,
                                     const std::string& stage,
                                     const std::string& label,
                                     uint32_t eid,
                                     uint32_t column_mask) {
    if (!shouldDumpTable(stage)) return;
    
    // Use existing debug_dump_with_mask function for now
    debug_dump_with_mask(table, label.c_str(),
                        (stage + "_" + label).c_str(),
                        eid, column_mask);
    
    tables_dumped++;
}

void DebugManager::dumpEntry(const Entry& entry,
                             const std::string& label,
                             uint32_t eid) {
    if (!config.tables.enabled || !session_active) return;
    
    // Use existing debug_dump_entry function for now
    debug_dump_entry(entry, label.c_str(), eid);
}

void DebugManager::startPhaseTimer(const std::string& phase) {
    if (!config.perf.per_phase) return;
    phase_timers[phase] = std::chrono::steady_clock::now();
}

void DebugManager::endPhaseTimer(const std::string& phase) {
    if (!config.perf.per_phase) return;
    
    auto it = phase_timers.find(phase);
    if (it != phase_timers.end()) {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - it->second);
        phase_durations[phase] = static_cast<double>(duration.count()) / 1000.0; // Convert to milliseconds
        phase_timers.erase(it);
    }
}

double DebugManager::getPhaseTime(const std::string& phase) const {
    auto it = phase_durations.find(phase);
    return (it != phase_durations.end()) ? it->second : 0.0;
}

void DebugManager::logPerformanceSummary() {
    if (phase_durations.empty()) return;
    
    std::stringstream ss;
    ss << "\n=== Performance Summary ===" << std::endl;
    
    double total_time = 0.0;
    for (const auto& entry : phase_durations) {
        const std::string& phase = entry.first;
        double duration = entry.second;
        ss << std::setw(20) << std::left << phase << ": "
           << std::fixed << std::setprecision(3) << duration << " ms" << std::endl;
        total_time += duration;
    }
    
    ss << std::setw(20) << std::left << "Total" << ": "
       << std::fixed << std::setprecision(3) << total_time << " ms" << std::endl;
    ss << "===========================" << std::endl;
    
    logToFile(ss.str());
    
    if (config.output_mode == DEBUG_OUTPUT_CONSOLE || config.output_mode == DEBUG_OUTPUT_BOTH) {
        printf("%s", ss.str().c_str());
    }
}

void DebugManager::resetStatistics() {
    tables_dumped = 0;
    logs_written = 0;
    phase_timers.clear();
    phase_durations.clear();
}

std::string DebugManager::formatBytes(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit < 3) {
        size /= 1024.0;
        unit++;
    }
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return ss.str();
}

std::string DebugManager::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_val = std::chrono::system_clock::to_time_t(now);
    struct tm* tm = localtime(&time_val);
    
    std::stringstream ss;
    ss << std::setfill('0')
       << (1900 + tm->tm_year) << "-"
       << std::setw(2) << (1 + tm->tm_mon) << "-"
       << std::setw(2) << tm->tm_mday << " "
       << std::setw(2) << tm->tm_hour << ":"
       << std::setw(2) << tm->tm_min << ":"
       << std::setw(2) << tm->tm_sec;
    return ss.str();
}

uint32_t DebugManager::getColumnMaskForStage(const std::string& stage) {
    // Return appropriate column mask based on stage
    // This can be customized based on configuration
    if (stage == "bottom_up") {
        return DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | 
               DEBUG_COL_LOCAL_CUMSUM | DEBUG_COL_LOCAL_INTERVAL |
               DEBUG_COL_FIELD_TYPE | DEBUG_COL_EQUALITY_TYPE | DEBUG_COL_JOIN_ATTR;
    } else if (stage == "top_down") {
        return DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_LOCAL_MULT | DEBUG_COL_FINAL_MULT |
               DEBUG_COL_FOREIGN_SUM | DEBUG_COL_FOREIGN_INTERVAL | DEBUG_COL_LOCAL_WEIGHT |
               DEBUG_COL_FIELD_TYPE | DEBUG_COL_EQUALITY_TYPE | DEBUG_COL_JOIN_ATTR;
    } else if (stage == "distribute") {
        return DEBUG_COL_INDEX | DEBUG_COL_DST_IDX | DEBUG_COL_ORIGINAL_INDEX |
               DEBUG_COL_FIELD_TYPE | DEBUG_COL_LOCAL_MULT | DEBUG_COL_FINAL_MULT;
    } else if (stage == "alignment") {
        return DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_COPY_INDEX | DEBUG_COL_ALIGNMENT_KEY |
               DEBUG_COL_FINAL_MULT | DEBUG_COL_JOIN_ATTR;
    }
    
    // Default to essential columns
    return DEBUG_COL_ORIGINAL_INDEX | DEBUG_COL_FIELD_TYPE | 
           DEBUG_COL_JOIN_ATTR | DEBUG_COL_LOCAL_MULT | DEBUG_COL_FINAL_MULT;
}