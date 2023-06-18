#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <map>
#include <string>
#include "nlohmann/json.hpp"
//----------------------------------------------------------------------------------------------------------------------
#include "modbus_client.h"
#include "modbus.h"
#include "chanlib_export.h"
#include "mpoller.h"
//----------------------------------------------------------------------------------------------------------------------
const char* defaultConf = (char*)"./config.xml";
uint16_t MBHR_COPY[MBHR_SETTINGS_SIZE];
//----------------------------------------------------------------------------------------------------------------------
static int parse_ip(string input);
static int parse_cert(string input);
static int parse_port(string input);
static int parse_user(string input);
static int parse_pwd(string input);
static int parse_usetls(string input);
//----------------------------------------------------------------------------------------------------------------------
typedef int (*parser_func)(string input);
//----------------------------------------------------------------------------------------------------------------------
static map<string,parser_func> paramMap = 
{
    std::pair{"broker_ip",parse_ip},
    std::pair{"broker_cert",parse_cert},
    std::pair{"broker_port",parse_port},
    std::pair{"broker_user",parse_user},
    std::pair{"broker_pwd",parse_pwd},
    std::pair{"use_tls",parse_usetls},
};
//----------------------------------------------------------------------------------------------------------------------
enum WorkState
{
    IDLE_STATE = 0,
    LOGSWITCH_SENT,
    WRITE_SENT,
    READ_SENT,
    SAVE_SENT,
    RESET_SENT,
    ERROR
};
//----------------------------------------------------------------------------------------------------------------------
#define BLOCK_SEND_TIMEOUT_MS       PACKET_SEND_TIMEOUT_MS
//----------------------------------------------------------------------------------------------------------------------
class MRes : public MPoller
{
public:
    MRes(mxml_node_t* cnf=nullptr):MPoller(cnf){}
    virtual ~MRes(){}
    virtual void thread_job();
    virtual void process_packet(uint8_t* data, int len);
    int send_write_reg(uint32_t addr, uint16_t value);
    int send_log_switch(int logon);
    WorkState state = IDLE_STATE;
    int logon=1;
};
//----------------------------------------------------------------------------------------------------------------------
class MConf : public MRes
{
public:
    MConf(mxml_node_t* cnf=nullptr):MRes(cnf){}
    virtual ~MConf(){}
    virtual void thread_job();
    virtual void process_packet(uint8_t* data, int len);
    int send_read_reg(uint32_t addr);
    //int send_write_reg(uint32_t addr, uint16_t value);
    int send_order_reg(uint32_t addr);
    int send_write_batch(uint32_t addr, int len);
    //int send_log_switch(int logon);
    //WorkState state = IDLE_STATE;
    int regaddr = BROKER_IP_ADDRESS_DATA_01;
    int wrlen=1;
    //int logon=1;
};
//----------------------------------------------------------------------------------------------------------------------
int MRes::send_write_reg(uint32_t addr, uint16_t value)
{
    timespec_t curStamp = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &curStamp);
    vector<uint8_t> packdata;
    packdata = MBCL->build_write_reg_06(MB_BROADCAST_ADDR, addr, value);
    if(packdata.size() <= 0)
    {
        CHPLWRITELOG("error constructing modbus packet, exiting...");
        state = ERROR;
        return -1;
    }
    fprintf(stderr, "WRITE: register %04d, value %04X\n",addr,value);
    std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
    memcpy(buf->Data(),packdata.data(),packdata.size());
    shared_ptr<BasicChannel> schan = currentSession->ch.lock();
    if(!schan)
    {
        state = ERROR;
        return -1;
    }
    schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
    packSentStamp = curStamp;
    state = WRITE_SENT;
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int MConf::send_order_reg(uint32_t addr)
{
    return send_write_reg(addr, MBHR_COPY[addr]);
}
//----------------------------------------------------------------------------------------------------------------------
int MConf::send_write_batch(uint32_t addr, int len)
{
    timespec_t curStamp = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &curStamp);
    vector<uint8_t> packdata;
    vector<uint16_t> regsToWrite;
    for(int i = 0; i < len; i++)
    {
        uint16_t dat=MBHR_COPY[i+addr];
        regsToWrite.push_back(dat);
    }
    packdata = MBCL->build_write_multreg_16(MB_BROADCAST_ADDR, addr, regsToWrite);
    if(packdata.size() <= 0)
    {
        CHPLWRITELOG("error constructing modbus packet, exiting...");
        state = ERROR;
        return -1;
    }
    fprintf(stderr, "WRITE: register %04d, len %d\n",addr,len);
    std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
    memcpy(buf->Data(),packdata.data(),packdata.size());
    shared_ptr<BasicChannel> schan = currentSession->ch.lock();
    if(!schan)
    {
        state = ERROR;
        return -1;
    }
    schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
    packSentStamp = curStamp;
    state = WRITE_SENT;
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int MRes::send_log_switch(int logon)
{
    timespec_t curStamp = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &curStamp);
    vector<uint8_t> packdata;
    packdata = MBCL->build_write_reg_06(MB_BROADCAST_ADDR, MBHR_LOG_ON, logon);
    if(packdata.size() <= 0)
    {
        CHPLWRITELOG("error constructing modbus packet, exiting...");
        state = ERROR;
        return -1;
    }
    std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
    memcpy(buf->Data(),packdata.data(),packdata.size());
    shared_ptr<BasicChannel> schan = currentSession->ch.lock();
    if(!schan)
    {
        state = ERROR;
        return -1;
    }
    schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
    packSentStamp = curStamp;
    state = LOGSWITCH_SENT;
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int MConf::send_read_reg(uint32_t addr)
{
    timespec_t curStamp = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &curStamp);
    vector<uint8_t> packdata;
    packdata = MBCL->build_read_03(MB_BROADCAST_ADDR, addr, 1);
    if(packdata.size() <= 0)
    {
        CHPLWRITELOG("error constructing modbus packet, exiting...");
        state = ERROR;
    }
    std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
    memcpy(buf->Data(),packdata.data(),packdata.size());
    shared_ptr<BasicChannel> schan = currentSession->ch.lock();
    if(!schan)
    {
        state = ERROR;
        return -1;
    }
    schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
    packSentStamp = curStamp;
    state = READ_SENT;
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
void MRes::process_packet(uint8_t* data, int len)
{
    uint16_t crc=0;
    uint16_t regval = 0;
    uint16_t status = 0;
    switch(state)
    {
    case IDLE_STATE:
        CHPLWRITELOG("unexpected error, exiting...");
        exit(-1);
        break;
    case LOGSWITCH_SENT:
        if(logon == 0)
        {
            fprintf(stderr, "board logging is off\n");
            state = IDLE_STATE;
        }
        else
        {
            fprintf(stderr, "board logging is BACK ON\n");
            exit(0);
        }
        break;
    case RESET_SENT:
        CHPLWRITELOG("erase command accepted, exiting...");
        send_log_switch(1);
        exit(0);
        break;
    default:
        CHPLWRITELOG("unknown state, exiting...");
        exit(-1);
        break;  
    return;
    }
}
//----------------------------------------------------------------------------------------------------------------------
void MConf::process_packet(uint8_t* data, int len)
{
    uint16_t crc=0;
    uint16_t regval = 0;
    uint16_t status = 0;
    switch(state)
    {
    case IDLE_STATE:
        CHPLWRITELOG("unexpected error, exiting...");
        exit(-1);
        break;
    case LOGSWITCH_SENT:
        if(logon == 0)
        {
            fprintf(stderr, "board logging is off\n");
            state = IDLE_STATE;
        }
        else
        {
            fprintf(stderr, "board logging is BACK ON\n");
            exit(0);
        }
        break;
    case WRITE_SENT:
        if(regaddr < MBHR_SETTINGS_LAST_ADDR)
            send_read_reg(regaddr);
        break;
    case READ_SENT:
        regval = (((uint16_t)data[MODBUS_03_DATASTART_IND] << 8) & 0xff00) | ((uint16_t)data[MODBUS_03_DATASTART_IND+1] & 0x00ff);
        //fprintf(stderr, "READ:  register %04d, value %04X\n",regaddr,regval);
        if(regval != MBHR_COPY[regaddr])
        {
            CHPLWRITELOG("write/read values dont match, exiting...");
            exit(-1);
        }
        regaddr+=wrlen;
        state = IDLE_STATE;
        break;
    case SAVE_SENT:
        CHPLWRITELOG("all values are written, exiting...");
        send_log_switch(1);
        exit(0);
        break;
    default:
        CHPLWRITELOG("unknown state, exiting...");
        exit(-1);
        break;  
    return;
    }
}
//----------------------------------------------------------------------------------------------------------------------
static int addr_to_write(uint16_t addr)
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
void MRes::thread_job()
{
    vector<uint8_t> packdata;
    uint64_t diff=0;
    timespec_t curStamp = {0,0};
    shared_ptr<BasicChannel> schan;
    std::unique_ptr<MessageBuffer> buf;
    if(currentSession == nullptr)
        return;
    vector<uint8_t> pack = get_packet();
    if(pack.size())
        process_packet((uint8_t*)pack.data(), pack.size());
    switch(state)
    {
    case IDLE_STATE:
        if(logon)
        {
            logon=0;
            send_log_switch(0);
            break;
        }
        send_write_reg(MBHR_REG_COMMAND,CMD_ERASE_SETTINGS);
        state = RESET_SENT;
        break;
    case RESET_SENT:
        clock_gettime(CLOCK_MONOTONIC, &curStamp);
        diff = ((uint64_t)curStamp.tv_sec*1000 + (uint64_t)curStamp.tv_nsec/DECMILLION) - \
                    ((uint64_t)packSentStamp.tv_sec*1000 + (uint64_t)packSentStamp.tv_nsec/DECMILLION);
        if(diff > BLOCK_SEND_TIMEOUT_MS)
        {
            CHPLWRITELOG("timeout error, retry...");
            state = IDLE_STATE;
        }            
        break;
    case LOGSWITCH_SENT:
        clock_gettime(CLOCK_MONOTONIC, &curStamp);
        diff = ((uint64_t)curStamp.tv_sec*1000 + (uint64_t)curStamp.tv_nsec/DECMILLION) - \
                    ((uint64_t)packSentStamp.tv_sec*1000 + (uint64_t)packSentStamp.tv_nsec/DECMILLION);
        if(diff > BLOCK_SEND_TIMEOUT_MS)
        {
            CHPLWRITELOG("timeout error, but it is logswitch...");
            state = IDLE_STATE;
            //exit(-1);
        }            
        break;
    case ERROR:     
        CHPLWRITELOG("unknown error, exiting...");
        exit(-1);
        break;
    default:
        CHPLWRITELOG("unknown state, exiting...");
        exit(-1);
        break;
    }
}
//----------------------------------------------------------------------------------------------------------------------
void MConf::thread_job()
{
    vector<uint8_t> packdata;
    uint64_t diff=0;
    timespec_t curStamp = {0,0};
    shared_ptr<BasicChannel> schan;
    std::unique_ptr<MessageBuffer> buf;
    if(currentSession == nullptr)
        return;
    vector<uint8_t> pack = get_packet();
    if(pack.size())
        process_packet((uint8_t*)pack.data(), pack.size());
    switch(state)
    {
    case IDLE_STATE:
        if(logon)
        {
            logon=0;
            send_log_switch(0);
            break;
        }
        if(regaddr < MBHR_SETTINGS_LAST_ADDR)
        {
            if(!addr_to_write(regaddr))
            {
                wrlen = 1;
                uint16_t regaddrfin=regaddr;
                while(!addr_to_write(regaddrfin++))
                {
                    wrlen++;
                    if(wrlen >= MAX_REGS_BATCH_WRITE)
                        break;
                }
                send_write_batch(regaddr,wrlen);
            }
            else
                regaddr++;
        }
        else
        {
            send_write_reg(MBHR_REG_COMMAND,CMD_WRITE_SETTINGS);
            state = SAVE_SENT;
        }
        break;
    case WRITE_SENT:
    case READ_SENT:
    case SAVE_SENT:
        clock_gettime(CLOCK_MONOTONIC, &curStamp);
        diff = ((uint64_t)curStamp.tv_sec*1000 + (uint64_t)curStamp.tv_nsec/DECMILLION) - \
                    ((uint64_t)packSentStamp.tv_sec*1000 + (uint64_t)packSentStamp.tv_nsec/DECMILLION);
        if(diff > BLOCK_SEND_TIMEOUT_MS)
        {
            CHPLWRITELOG("timeout error, retry...");
            state = IDLE_STATE;
        }            
        break;
    case LOGSWITCH_SENT:
        clock_gettime(CLOCK_MONOTONIC, &curStamp);
        diff = ((uint64_t)curStamp.tv_sec*1000 + (uint64_t)curStamp.tv_nsec/DECMILLION) - \
                    ((uint64_t)packSentStamp.tv_sec*1000 + (uint64_t)packSentStamp.tv_nsec/DECMILLION);
        if(diff > BLOCK_SEND_TIMEOUT_MS)
        {
            CHPLWRITELOG("timeout error, but it is logswitch...");
            state = IDLE_STATE;
            //exit(-1);
        }            
        break;
    case ERROR:     
        CHPLWRITELOG("unknown error, exiting...");
        exit(-1);
        break;
    default:
        CHPLWRITELOG("unknown state, exiting...");
        exit(-1);
        break;
    }
}
//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    string filename;
    string configfile;
    int option_index = 0;
    int c;
    int needreset=0;
    static struct option long_options[] = {
            {"input",     required_argument,NULL,0},
            {"config",  required_argument,NULL,0},
            {"reset",  no_argument,NULL,0},
            {NULL,    0,                 NULL,  0 }
    };
    while((c = getopt_long(argc, argv, "i:c:r",long_options,&option_index)) != -1)
    {
        switch(c)
        {
            case 0:
                if(option_index == 0)
                {
                    filename = string(optarg);
                }
                else if(option_index == 1)
                {
                    configfile = string(optarg);
                }
                break;
            case 'i':
                filename = string(optarg);
                break;
            case 'c':
                configfile = string(optarg);
                break;
            case 'r':
                needreset=1;
                break;
            case '?':
                fprintf(stderr, "unknown option: %c\n", option_index);
                break;
        }
    }
    
    if(configfile == "")
    {
        configfile = string(defaultConf);
    }
    bool confok = 1;
    FILE* fpconf=nullptr;
    mxml_node_t* tree = nullptr;
    if ((fpconf = fopen(configfile.c_str(), "r")) != NULL)        // открыть файл с конфигурацией в формате XML
    {
        tree = mxmlLoadFile (nullptr, fpconf, MXML_NO_CALLBACK);       // считать конфигурацию
        fclose(fpconf);
        if (tree == nullptr)
        {
            fprintf(stderr, "comport file invalid\n");
            confok = 0;
            return -1;
        }
    }
    else
    {
        fprintf(stderr, "cant open comport file\n");
        confok = 0;
        return -1;
    }
    if(needreset)
    {
        shared_ptr<MRes> fres = shared_ptr<MRes>(new MRes(tree));
        fres->init_module();
        while(!fres->stop);
    }
    if(filename == "")
    {
        fprintf(stderr, "no board config file provided\n");
        return -1;
    }
    if(!std::filesystem::exists(filename))
    {
        fprintf(stderr, "board config file don\'t exist\n");
        return -1;
    }
    memset(MBHR_COPY,0,MBHR_SETTINGS_SIZE*2);
    std::ifstream ifs(filename);
    nlohmann::json jf = nlohmann::json::parse(ifs);
    if(jf.is_discarded())
    {
        fprintf(stderr, "board config file is invalid\n");
        return -1;
    }
    cout << std::setw(4) << jf << std::endl;
    for (auto it=jf.begin(); it!=jf.end(); it++)
    {
        map<string,parser_func>::iterator pit = paramMap.find(it.key());
        if(pit != paramMap.end())
        {
            if(pit->second(it.value()))
            {
                cout << it.key() << ": parsing failed" << endl;
                exit(-1);
            }
        }
    }
    shared_ptr<MConf> fupd = shared_ptr<MConf>(new MConf(tree));
    fupd->init_module();
    while(!fupd->stop);
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
static int parse_ip(string input)
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
static int parse_cert(string input)
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
static int parse_port(string input)
{
    uint8_t* data = (uint8_t*)&MBHR_COPY[BROKER_PORT_DATA_01];
    *(uint16_t*)data = atoi(input.data());
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
static int parse_user(string input)
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
static int parse_pwd(string input)
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
static int parse_usetls(string input)
{
    uint8_t* data = (uint8_t*)&MBHR_COPY[USE_TLS_DATA_01];
    *(uint16_t*)data = atoi(input.data());
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------