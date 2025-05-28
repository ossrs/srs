//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_python_manager.hpp>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_config.hpp>
#include <srs_app_process.hpp>
#include <srs_kernel_utility.hpp>
#include <sstream>
#include <unistd.h>
#include <cstdio>
#include <cstring>

SrsPythonManager::SrsPythonManager()
{
    enabled_ = false;
    disposed_ = false;
}

SrsPythonManager::~SrsPythonManager()
{
    dispose();
}

srs_error_t SrsPythonManager::initialize()
{
    srs_error_t err = srs_success;
    
    if ((err = parse_config()) != srs_success) {
        return srs_error_wrap(err, "parse python config");
    }
    
    return err;
}

srs_error_t SrsPythonManager::start()
{
    srs_error_t err = srs_success;
    
    if (!enabled_) {
        return err;
    }
    
    srs_trace("Python manager starting %d processes", (int)processes_.size());
    
    for (std::vector<SrsProcess*>::iterator it = processes_.begin(); it != processes_.end(); ++it) {
        SrsProcess* process = *it;
        
        if ((err = process->start()) != srs_success) {
            srs_error("Failed to start Python process: %s", srs_error_desc(err).c_str());
            // Continue starting other processes even if one fails
            srs_freep(err);
            continue;
        }
        
        srs_trace("Python process started, pid=%d", process->get_pid());
    }
    
    srs_trace("Python manager started successfully");
    return err;
}

void SrsPythonManager::stop()
{
    if (!enabled_ || disposed_) {
        return;
    }
    
    srs_trace("Python manager stopping %d processes", (int)processes_.size());
    
    // First, send SIGTERM to all processes for graceful shutdown
    for (std::vector<SrsProcess*>::iterator it = processes_.begin(); it != processes_.end(); ++it) {
        SrsProcess* process = *it;
        if (process->started()) {
            process->fast_stop();
            srs_trace("Sent SIGTERM to Python process, pid=%d", process->get_pid());
        }
    }
    
    // Wait a moment for graceful shutdown
    srs_usleep(500 * SRS_UTIME_MILLISECONDS);
    
    // Then stop all processes (will SIGKILL if necessary)
    for (std::vector<SrsProcess*>::iterator it = processes_.begin(); it != processes_.end(); ++it) {
        SrsProcess* process = *it;
        if (process->started()) {
            process->stop();
            srs_trace("Python process stopped, pid=%d", process->get_pid());
        }
    }
    
    srs_trace("Python manager stopped");
}

void SrsPythonManager::dispose()
{
    if (disposed_) {
        return;
    }
    disposed_ = true;
    
    stop();
    
    // Clean up all process objects
    for (std::vector<SrsProcess*>::iterator it = processes_.begin(); it != processes_.end(); ++it) {
        SrsProcess* process = *it;
        srs_freep(process);
    }
    processes_.clear();
    
    srs_trace("Python manager disposed");
}

srs_error_t SrsPythonManager::parse_config()
{
    srs_error_t err = srs_success;
    
    // Check if Python addons are enabled in config
    SrsConfDirective* conf = _srs_config->get_python_addons_on();
    if (!conf || !_srs_config->get_python_addons_enabled()) {
        enabled_ = false;
        srs_trace("Python addon manager disabled in config");
        return err;
    }
    
    enabled_ = true;
    srs_trace("Python addon manager enabled in config");
    
    // Get Python executable path from built environment
    std::string python_exe = get_builtin_python_path();
    if (python_exe.empty()) {
        srs_warn("Built-in Python environment not found, disabling Python addons");
        enabled_ = false;
        return err;
    }
    
    // Get Python addon processes configuration
    std::vector<SrsConfDirective*> addon_confs = _srs_config->get_python_addons_processes();
    srs_trace("Parsed %d Python addons from config", (int)addon_confs.size());
    
    for (std::vector<SrsConfDirective*>::iterator it = addon_confs.begin(); it != addon_confs.end(); ++it) {
        SrsConfDirective* addon_conf = *it;
        
        // Get script path
        SrsConfDirective* script_conf = addon_conf->get("script");
        if (!script_conf || script_conf->args.empty()) {
            srs_warn("Python addon missing script directive, skipping");
            continue;
        }
        std::string script_path = script_conf->arg0();
        
        // Build command line arguments
        std::vector<std::string> args;
        args.push_back(python_exe);    // Use built-in Python executable
        args.push_back(script_path);   // Script path
        
        // Add additional arguments if specified
        SrsConfDirective* args_conf = addon_conf->get("args");
        if (args_conf && !args_conf->args.empty()) {
            // Split the args string into individual arguments
            std::string args_str = args_conf->arg0();
            std::istringstream iss(args_str);
            std::string arg;
            while (iss >> arg) {
                args.push_back(arg);
            }
        }
        
        // Get working directory (optional)
        std::string work_dir = "./";
        SrsConfDirective* work_dir_conf = addon_conf->get("work_dir");
        if (work_dir_conf && !work_dir_conf->args.empty()) {
            work_dir = work_dir_conf->arg0();
        }
        
        if ((err = create_process(script_path, args, work_dir)) != srs_success) {
            return srs_error_wrap(err, "create python addon process for %s", script_path.c_str());
        }
    }
    
    srs_trace("Parsed %d Python addon processes from config", (int)processes_.size());
    return err;
}

srs_error_t SrsPythonManager::create_process(const std::string& script_path, const std::vector<std::string>& args, const std::string& work_dir)
{
    srs_error_t err = srs_success;
    
    SrsProcess* process = new SrsProcess();
    
    // Initialize the process with python executable and arguments
    if ((err = process->initialize(args[0], args)) != srs_success) {
        srs_freep(process);
        return srs_error_wrap(err, "initialize python addon process");
    }
    
    processes_.push_back(process);
    srs_trace("Created Python addon process for script: %s with executable: %s, work_dir: %s", 
        script_path.c_str(), args[0].c_str(), work_dir.c_str());
    
    return err;
}

std::string SrsPythonManager::get_builtin_python_path()
{
    // Try to read the Python path from the file created during build
    std::string python_path_file = "./objs/python_path.txt";
    
    FILE* fp = fopen(python_path_file.c_str(), "r");
    if (!fp) {
        srs_warn("Cannot open Python path file: %s", python_path_file.c_str());
        return "";
    }
    
    char path[1024] = {0};
    if (fgets(path, sizeof(path), fp) != NULL) {
        // Remove newline characters
        size_t len = strlen(path);
        while (len > 0 && (path[len-1] == '\n' || path[len-1] == '\r')) {
            path[--len] = '\0';
        }
        fclose(fp);
        
        // Verify the Python executable exists
        if (access(path, X_OK) == 0) {
            srs_trace("Using built-in Python: %s", path);
            return std::string(path);
        } else {
            srs_warn("Built-in Python executable not accessible: %s", path);
        }
    } else {
        srs_warn("Failed to read Python path from file: %s", python_path_file.c_str());
    }
    
    fclose(fp);
    
    // Fallback: try common locations
    const char* fallback_paths[] = {
        "./objs/python_venv/bin/python",
        "./objs/python_venv/Scripts/python.exe",
        "python3",
        "python"
    };
    
    for (int i = 0; i < 4; i++) {
        if (access(fallback_paths[i], X_OK) == 0) {
            srs_trace("Using fallback Python: %s", fallback_paths[i]);
            return std::string(fallback_paths[i]);
        }
    }
    
    srs_warn("No suitable Python executable found");
    return "";
}
