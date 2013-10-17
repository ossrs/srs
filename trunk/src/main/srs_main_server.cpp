#include <unistd.h>

#include <srs_core_log.hpp>

int main(int /*argc*/, char** /*argv*/){
	log_context->SetId();
	
	SrsWarn("server start");
	
    return 0;
}
