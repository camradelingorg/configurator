#ifndef PARAMETERS_MAP_H
#define PARAMETERS_MAP_H
//----------------------------------------------------------------------------------------------------------------------
#include <string>
#include <map>
//----------------------------------------------------------------------------------------------------------------------
typedef function<int(string input)> parser_func;
//----------------------------------------------------------------------------------------------------------------------
class ParametersMap
{
public:
    ParametersMap();
    ~ParametersMap(){}
    int parse_ip(string input);
    int parse_cert(string input);
    int parse_port(string input);
    int parse_user(string input);
    int parse_pwd(string input);
    int parse_usetls(string input);
    int addr_to_write(uint16_t addr);
    map<string,parser_func> pmap;
    uint16_t MBHR_COPY[MBHR_SETTINGS_SIZE];
};
//----------------------------------------------------------------------------------------------------------------------
#endif /*PARAMETERS_MAP_H*/