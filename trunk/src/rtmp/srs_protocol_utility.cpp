/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_protocol_utility.hpp>

void srs_vhost_resolve(std::string& vhost, std::string& app)
{
    app = srs_replace(app, "...", "?");
    
    size_t pos = 0;
    if ((pos = app.find("?")) == std::string::npos) {
        return;
    }
    
    std::string query = app.substr(pos + 1);
    app = app.substr(0, pos);
    
    if ((pos = query.find("vhost?")) != std::string::npos
        || (pos = query.find("vhost=")) != std::string::npos
        || (pos = query.find("Vhost?")) != std::string::npos
        || (pos = query.find("Vhost=")) != std::string::npos
    ) {
        query = query.substr(pos + 6);
        if (!query.empty()) {
            vhost = query;
        }
    }
}
