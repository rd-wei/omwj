#ifndef DEBUG_MANAGER_H
#define DEBUG_MANAGER_H

#include <string>
#include <memory>
#include <fstream>
#include <chrono>
#include <map>
#include <vector>
#include "debug_config.h"
#include "debug_util.h"
#include "data_structures/table.h"

// Forward declarations
struct Entry;

/**
 * DebugManager - Centralized debug control system
 * 
 * Singleton class that manages all debug operations including:
 * - Configuration management
 * - Session control
 * - Conditional logging
 * - Table dumping
 * - Performance tracking
 */
class DebugManager {
private:
    // Singleton instance
    static std::unique_ptr<DebugManager> instance;
    
    // Current configuration
    DebugConfig config;
    
    // Session management
    bool session_active;
    std::string session_name;
    std::string session_dir;
    std::ofstream log_file;
    std::chrono::steady_clock::time_point session_start;
    
    // Performance tracking
    std::map<std::string, std::chrono::steady_clock::time_point> phase_timers;
    std::map<std::string, double> phase_durations;
    
    // Statistics
    size_t tables_dumped;
    size_t logs_written;
    
    // Private constructor for singleton
    DebugManager();
    
    // Helper methods
    std::string generateSessionDir(const std::string& name);
    std::string generateFilename(const std::string& base, const std::string& extension);
    void writeLogHeader();
    void writeLogFooter();
    
public:
    // Singleton access
    static DebugManager& getInstance();
    
    // Prevent copying
    DebugManager(const DebugManager&) = delete;
    DebugManager& operator=(const DebugManager&) = delete;
    
    // Configuration management
    void loadConfig(const std::string& config_file);
    void loadConfigFromEnvironment();
    void setConfig(const DebugConfig& cfg);
    const DebugConfig& getConfig() const { return config; }
    
    // Dynamic configuration updates
    void setDebugLevel(uint32_t level);
    void enablePhase(const std::string& phase, bool enable);
    void enableTableDumps(bool enable);
    
    // Session management
    void startSession(const std::string& name);
    void endSession();
    bool isSessionActive() const { return session_active; }
    const std::string& getSessionName() const { return session_name; }
    const std::string& getSessionDir() const { return session_dir; }
    
    // Conditional checks
    bool shouldLog(uint32_t level) const;
    bool shouldLog(uint32_t level, const std::string& module) const;
    bool shouldDumpTable(const std::string& stage) const;
    bool isPhaseEnabled(const std::string& phase) const;
    
    // Logging interface
    void log(uint32_t level, const char* file, int line, const char* fmt, ...);
    void logToFile(const std::string& message);
    
    // Table dumping interface
    void dumpTable(const Table& table, 
                   const std::string& stage,
                   const std::string& label,
                   uint32_t eid = 0,
                   const std::vector<MetadataColumn>& columns = {},
                   bool include_attributes = false);
    
    void dumpTableWithMask(const Table& table,
                           const std::string& stage,
                           const std::string& label,
                           uint32_t eid,
                           uint32_t column_mask);
    
    void dumpEntry(const Entry& entry,
                   const std::string& label,
                   uint32_t eid = 0);
    
    // Performance tracking
    void startPhaseTimer(const std::string& phase);
    void endPhaseTimer(const std::string& phase);
    double getPhaseTime(const std::string& phase) const;
    void logPerformanceSummary();
    
    // Statistics
    size_t getTablesDumped() const { return tables_dumped; }
    size_t getLogsWritten() const { return logs_written; }
    void resetStatistics();
    
    // Utility methods
    static std::string formatBytes(size_t bytes);
    static std::string getCurrentTimestamp();
    static uint32_t getColumnMaskForStage(const std::string& stage);
};

// Convenience macros that use DebugManager
#define DEBUG_MGR DebugManager::getInstance()

// Module-aware logging macros
#define DEBUG_LOG(level, fmt, ...) \
    if (DEBUG_MGR.shouldLog(level)) \
        DEBUG_MGR.log(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_LOG_MODULE(level, module, fmt, ...) \
    if (DEBUG_MGR.shouldLog(level, module)) \
        DEBUG_MGR.log(level, __FILE__, __LINE__, "[%s] " fmt, module, ##__VA_ARGS__)

// Conditional table dumping
#define DEBUG_DUMP_TABLE_IF(stage, table, label, eid, ...) \
    if (DEBUG_MGR.shouldDumpTable(stage)) \
        DEBUG_MGR.dumpTable(table, stage, label, eid, ##__VA_ARGS__)

// Phase timing macros
#define DEBUG_PHASE_START(phase) \
    if (DEBUG_MGR.getConfig().perf.per_phase) \
        DEBUG_MGR.startPhaseTimer(phase)

#define DEBUG_PHASE_END(phase) \
    if (DEBUG_MGR.getConfig().perf.per_phase) \
        DEBUG_MGR.endPhaseTimer(phase)

// Quick enable/disable for specific modules
#define DEBUG_IF_PHASE(phase, code) \
    if (DEBUG_MGR.isPhaseEnabled(phase)) { code }

#endif // DEBUG_MANAGER_H