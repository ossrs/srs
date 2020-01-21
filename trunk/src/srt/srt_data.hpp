#ifndef SRT_DATA_H
#define SRT_DATA_H
#include <string>
#include <memory>

class SRT_DATA_MSG {
public:
    SRT_DATA_MSG(unsigned int len, const std::string& path);
    SRT_DATA_MSG(unsigned char* data_p, unsigned int len, const std::string& path);
    ~SRT_DATA_MSG();

    unsigned int data_len();
    unsigned char* get_data();
    std::string get_path();

private:
    unsigned int   _len;
    unsigned char* _data_p;
    std::string _key_path;
};

typedef std::shared_ptr<SRT_DATA_MSG> SRT_DATA_MSG_PTR;

#endif