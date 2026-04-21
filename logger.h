#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <pthread.h>
#include <iomanip>
#include <sys/types.h>
#include <unistd.h>


enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};


class Logger {
private:
    std::ofstream logFile;
    pthread_mutex_t logMutex;
    
    // Private constructor for Singleton pattern
    Logger() {
        pthread_mutex_init(&logMutex, NULL);
    }


    // Formats the current UTC time to ISO 8601-like string.
    std::string getUTCTime() {
        time_t now = time(0);
        tm *gmtm = gmtime(&now);
        char buf[25];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtm);
        return std::string(buf);
    }

    std::string levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG:   return "DEBUG";
            case LogLevel::INFO:    return "INFO";
            case LogLevel::WARNING: return "WARNING";
            case LogLevel::ERROR:   return "ERROR";
            default:                return "LOG";
        }
    }

public:
    // Delete copy constructor and assignment for Singleton integrity
    Logger(const Logger&) = delete;
    void operator=(const Logger&) = delete;

    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }


    // Opens the log file. 
    void init(const std::string& filename = "game.log") {
        logFile.open(filename, std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file!" << std::endl;
        }
    }


    // Core logging function.
    void log(LogLevel level, const std::string& func, const std::string& message) {
        pthread_mutex_lock(&logMutex); 

        std::string timeStr = getUTCTime();
        std::string levelStr = levelToString(level);
        pthread_t tid = pthread_self(); // Thread ID 

        // Format: [UTC Time] [TID] [LEVEL] [FUNCTION] Message
        std::string formatted = "[" + timeStr + " UTC] " +
                                "[TID: " + std::to_string((unsigned long)tid) + "] " +
                                "[" + levelStr + "] " +
                                "[" + func + "] " + 
                                message;

        // Write to game.log 
        if (logFile.is_open()) {
            logFile << formatted << std::endl;
        }

        // Also output to console for real-time monitoring
        std::cout << formatted << std::endl;

        pthread_mutex_unlock(&logMutex);
    }

    ~Logger() {
        if (logFile.is_open()) {
            logFile.close();
        }
        pthread_mutex_destroy(&logMutex);
    }
};


#define LOG_DEBUG(msg) Logger::getInstance().log(LogLevel::DEBUG, __FUNCTION__, msg)
#define LOG_INFO(msg)  Logger::getInstance().log(LogLevel::INFO,  __FUNCTION__, msg)
#define LOG_ERROR(msg) Logger::getInstance().log(LogLevel::ERROR, __FUNCTION__, msg)
#define LOG_WARNING(msg) Logger::getInstance().log(LogLevel::WARNING, __FUNCTION__, msg)

#endif