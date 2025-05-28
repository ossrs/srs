//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_PYTHON_MANAGER_HPP
#define SRS_APP_PYTHON_MANAGER_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsProcess;

// Manage Python processes that run alongside SRS server.
// This manager starts Python processes when SRS starts and stops them during graceful shutdown.
class SrsPythonManager
{
private:
    bool enabled_;
    bool disposed_;
    std::vector<SrsProcess*> processes_;
    
public:
    SrsPythonManager();
    virtual ~SrsPythonManager();
    
public:
    // Initialize the Python manager with configuration.
    virtual srs_error_t initialize();
    
    // Start all configured Python processes.
    virtual srs_error_t start();
    
    // Stop all Python processes gracefully.
    virtual void stop();
    
    // Dispose and cleanup all resources.
    virtual void dispose();
    
private:
    // Parse configuration and create Python processes.
    virtual srs_error_t parse_config();
    
    // Create a single Python process from configuration.
    virtual srs_error_t create_process(const std::string& script_path, const std::vector<std::string>& args, const std::string& work_dir = "./");
    
    // Get the path to the built-in Python executable.
    virtual std::string get_builtin_python_path();
};

#endif
