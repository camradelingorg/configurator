#ifndef CONFIGURATOR_H
#define CONFIGURATOR_H
//----------------------------------------------------------------------------------------------------------------------
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
#include <functional>
#include "nlohmann/json.hpp"
//----------------------------------------------------------------------------------------------------------------------
#include "modbus_client.h"
#include "modbus.h"
#include "chanlib_export.h"
#include "mpoller.h"
#include "parameters_map.h"
//----------------------------------------------------------------------------------------------------------------------
enum WorkState
{
    IDLE_STATE = 0,
    READ_SENT,
    WRITE_SENT,
    ERROR
};
//----------------------------------------------------------------------------------------------------------------------
#define BLOCK_SEND_TIMEOUT_MS       PACKET_SEND_TIMEOUT_MS
//----------------------------------------------------------------------------------------------------------------------
class Configurator : public MPoller
{
public:
    Configurator(mxml_node_t* cnf=nullptr);
    virtual ~Configurator(){}
    virtual void thread_job();
    virtual void process_packet(uint8_t* data, int len);
    int write_reg(uint32_t addr, uint16_t value);
    int read_reg(uint32_t addr, int len);
    int write_batch(uint32_t addr, int len);
    int write_configuration();
    bool timeout_expired();
    WorkState state = IDLE_STATE;
    pthread_mutex_t stateMut;
    int timeoutMs=PACKET_SEND_TIMEOUT_MS;
    int16_t curRegIndex = -1;
    shared_ptr<ParametersMap> mmap;
};
//----------------------------------------------------------------------------------------------------------------------
#endif/*CONFIGURATOR_H*/