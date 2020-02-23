#include <srs_stun_stack.hpp>

using namespace std;

SrsStunPacket::SrsStunPacket()
{
}

SrsStunPacket::~SrsStunPacket()
{
}

string SrsStunPacket::username()
{
    return "";
}

string SrsStunPacket::password()
{
    return "";
}

srs_error_t SrsStunPacket::decode(const char* buf, const int& nb_buf)
{
    srs_error_t err = srs_success;

    return err;
}
