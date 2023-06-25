#include <string>
#include "configurator.h"
#include "parameters_map.h"
//----------------------------------------------------------------------------------------------------------------------
ParametersMap::ParametersMap()
{
    pmap.insert(std::make_pair<string,parser_func>("broker_ip",bind(&ParametersMap::parse_ip,this,std::placeholders::_1)));
    pmap.insert(std::make_pair<string,parser_func>("broker_cert",bind(&ParametersMap::parse_cert,this,std::placeholders::_1)));
    pmap.insert(std::make_pair<string,parser_func>("broker_port",bind(&ParametersMap::parse_port,this,std::placeholders::_1)));
    pmap.insert(std::make_pair<string,parser_func>("broker_user",bind(&ParametersMap::parse_user,this,std::placeholders::_1)));
    pmap.insert(std::make_pair<string,parser_func>("broker_pwd",bind(&ParametersMap::parse_pwd,this,std::placeholders::_1)));
    pmap.insert(std::make_pair<string,parser_func>("use_tls",bind(&ParametersMap::parse_usetls,this,std::placeholders::_1)));
}
//----------------------------------------------------------------------------------------------------------------------
int ParametersMap::addr_to_write(uint16_t addr)
{
    if(addr >= BROKER_IP_ADDRESS_DATA_01 && addr <= BROKER_IP_ADDRESS_DATA_23 ||
        addr == BROKER_PORT_DATA_01 || 
        addr == BROKER_CERT_LENGTH_DATA_01 ||
        addr >= BROKER_CERT_DATA_01 && addr <= BROKER_CERT_DATA_LAST ||
        addr == BROKER_USER_LENGTH_DATA_01 ||
        addr >= BROKER_USER_DATA_01 && addr <= BROKER_USER_DATA_LAST ||
        addr == BROKER_PASSWORD_LENGTH_DATA_01 ||
        addr >= BROKER_PASSWORD_DATA_01 && addr <= BROKER_PASSWORD_DATA_LAST ||
        addr == USE_TLS_DATA_01
        )
        return 0;
    else return -1;
}
//----------------------------------------------------------------------------------------------------------------------
int ParametersMap::parse_ip(string input)
{
    unsigned char value[4] = {0};
    size_t index = 0;
    const char* str = input.c_str();
    for(int i = 0; i < input.length(); i++)
    {
        if (isdigit(str[i])) 
        {
            value[index] *= 10;
            value[index] += str[i] - '0';
        } 
        else 
        {
            index++;
        }
    }
    uint8_t* data = (uint8_t*)&MBHR_COPY[BROKER_IP_ADDRESS_DATA_01];
    memcpy(data,value,4);
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int ParametersMap::parse_cert(string input)
{
    uint8_t* data = (uint8_t*)&MBHR_COPY[BROKER_CERT_DATA_01];
    if(!input.length())
        return -1;
    memcpy(data,input.data(),input.length());
    uint16_t* len = &MBHR_COPY[BROKER_CERT_LENGTH_DATA_01];
    *len = input.length();
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int ParametersMap::parse_port(string input)
{
    uint8_t* data = (uint8_t*)&MBHR_COPY[BROKER_PORT_DATA_01];
    *(uint16_t*)data = atoi(input.data());
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int ParametersMap::parse_user(string input)
{
    uint8_t* data = (uint8_t*)&MBHR_COPY[BROKER_USER_DATA_01];
    if(!input.length())
        return -1;
    memcpy(data,input.data(),input.length());
    uint16_t* len = &MBHR_COPY[BROKER_USER_LENGTH_DATA_01];
    *len = input.length();
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int ParametersMap::parse_pwd(string input)
{
    uint8_t* data = (uint8_t*)&MBHR_COPY[BROKER_PASSWORD_DATA_01];
    if(!input.length())
        return -1;
    memcpy(data,input.data(),input.length());
    uint16_t* len = &MBHR_COPY[BROKER_PASSWORD_LENGTH_DATA_01];
    *len = input.length();
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int ParametersMap::parse_usetls(string input)
{
    uint8_t* data = (uint8_t*)&MBHR_COPY[USE_TLS_DATA_01];
    *(uint16_t*)data = atoi(input.data());
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
