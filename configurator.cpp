#include <stdint.h>
#include "configurator.h"
//----------------------------------------------------------------------------------------------------------------------
Configurator::Configurator(mxml_node_t* cnf):MPoller(cnf)
{
    mmap = shared_ptr<ParametersMap>(new ParametersMap);
    pthread_mutexattr_t     stateMutAttr;
    pthread_mutexattr_init (&stateMutAttr);
    pthread_mutexattr_settype(&stateMutAttr, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init (&stateMut, &stateMutAttr);
}
//----------------------------------------------------------------------------------------------------------------------
bool Configurator::timeout_expired()
{
    timespec_t curStamp = {0,0};
    uint64_t diff=0;
    bool ret = false;
    clock_gettime(CLOCK_MONOTONIC, &curStamp);
    diff = ((uint64_t)curStamp.tv_sec*1000 + (uint64_t)curStamp.tv_nsec/DECMILLION) - \
                ((uint64_t)packSentStamp.tv_sec*1000 + (uint64_t)packSentStamp.tv_nsec/DECMILLION);
    if(diff > PACKET_SEND_TIMEOUT_MS)
    {
        CHPLWRITELOG("timeout error, retry...\n");
        pthread_mutex_lock(&stateMut);
        state = IDLE_STATE;
        pthread_mutex_unlock(&stateMut);
        ret = true;
    }
    return ret; 
}
//----------------------------------------------------------------------------------------------------------------------
int Configurator::write_reg(uint32_t addr, uint16_t value)
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
    fprintf(stderr, "WRITE: register %04d, value %04X ... ",addr,value);
    std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
    memcpy(buf->Data(),packdata.data(),packdata.size());
    shared_ptr<BasicChannel> schan = currentSession->ch.lock();
    if(!schan)
    {
        state = ERROR;
        return -1;
    }
    pthread_mutex_lock(&stateMut);
    state = WRITE_SENT;
    pthread_mutex_unlock(&stateMut);
    schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
    packSentStamp = curStamp;
    int ret = 0;
    while(state != IDLE_STATE)
    {
        if(timeout_expired())
            ret = -1;
        usleep(10000);
    }
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int Configurator::read_reg(uint32_t addr, int len)
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
    fprintf(stderr, "READ: register %04d, len %d ... ",addr, len);
    std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
    memcpy(buf->Data(),packdata.data(),packdata.size());
    shared_ptr<BasicChannel> schan = currentSession->ch.lock();
    if(!schan)
    {
        state = ERROR;
        return -1;
    }
    pthread_mutex_lock(&stateMut);
    state = READ_SENT;
    pthread_mutex_unlock(&stateMut);
    schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
    packSentStamp = curStamp;
    curRegIndex = addr;
    int ret = 0;
    while(state != IDLE_STATE)
    {
        if(timeout_expired())
            ret = -1;
        usleep(10000);
    }
    curRegIndex = -1;
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int Configurator::write_batch(uint32_t addr, int len)
{
    timespec_t curStamp = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &curStamp);
    vector<uint8_t> packdata;
    vector<uint16_t> regsToWrite;
    for(int i = 0; i < len; i++)
    {
        uint16_t dat=mmap->MBHR_COPY[i+addr];
        regsToWrite.push_back(dat);
    }
    packdata = MBCL->build_write_multreg_16(MB_BROADCAST_ADDR, addr, regsToWrite);
    if(packdata.size() <= 0)
    {
        CHPLWRITELOG("error constructing modbus packet, exiting...");
        state = ERROR;
        return -1;
    }
    fprintf(stderr, "WRITE: register %04d, len %d ... ",addr,len);
    std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
    memcpy(buf->Data(),packdata.data(),packdata.size());
    shared_ptr<BasicChannel> schan = currentSession->ch.lock();
    if(!schan)
    {
        state = ERROR;
        return -1;
    }
    pthread_mutex_lock(&stateMut);
    state = WRITE_SENT;
    pthread_mutex_unlock(&stateMut);
    schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
    packSentStamp = curStamp;
    int ret = 0;
    while(state != IDLE_STATE)
    {
        if(timeout_expired())
            ret = -1;
        usleep(10000);
    }
    return ret;
}
//----------------------------------------------------------------------------------------------------------------------
int Configurator::write_configuration()
{
    int regaddr = BROKER_IP_ADDRESS_DATA_01;
    int wrlen=1;
    while(regaddr < MBHR_SETTINGS_LAST_ADDR)
    {
        if(!mmap->addr_to_write(regaddr))
        {
            wrlen = 1;
            uint16_t regaddrfin=regaddr;
            while(!mmap->addr_to_write(regaddrfin++))
            {
                wrlen++;
                if(wrlen >= MAX_REGS_BATCH_WRITE)
                    break;
            }
            if(write_batch(regaddr,wrlen))//non-zero status = error
                return -1;
            regaddr += wrlen;
        }
        else
            regaddr++;
    }
    if(write_reg(MBHR_REG_COMMAND,CMD_WRITE_SETTINGS))//non-zero status = error
        return -1;
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
void Configurator::process_packet(uint8_t* data, int len)
{
    uint16_t crc=0;
    uint16_t regval = 0;
    uint16_t status = 0;
    int reglen = 0;
    switch(state)
    {
    case IDLE_STATE:
        CHPLWRITELOG("unexpected error, exiting...");
        exit(-1);
        break;
    case WRITE_SENT:
        pthread_mutex_lock(&stateMut);
        state = IDLE_STATE;
        pthread_mutex_unlock(&stateMut);
        fprintf(stderr,"OK\n");
        break;
    case READ_SENT:
        reglen = data[MODBUS_03_LENGTH_IND]/2;
        for(int i = 0; i < reglen; i++)
        {
            if(curRegIndex+i < MBHR_SPACE_SIZE)
            {
                mmap->MBHR_COPY[curRegIndex+i] = (((uint16_t)data[MODBUS_03_DATASTART_IND+i*2] << 8) & 0xff00) | ((uint16_t)data[MODBUS_03_DATASTART_IND+i*2+1] & 0x00ff);
            }
            else
            {
                CHPLWRITELOG("exceeded modbus address space ...");
                exit(-1);
            }
        }
        pthread_mutex_lock(&stateMut);
        state = IDLE_STATE;
        pthread_mutex_unlock(&stateMut);
        fprintf(stderr,"OK\n");
        break;
    default:
        CHPLWRITELOG("unknown state, exiting...");
        exit(-1);
        break;  
    return;
    }
}
//----------------------------------------------------------------------------------------------------------------------
void Configurator::thread_job()
{
    vector<uint8_t> packdata;
    timespec_t curStamp = {0,0};
    shared_ptr<BasicChannel> schan;
    std::unique_ptr<MessageBuffer> buf;
    if(currentSession == nullptr)
        return;
    vector<uint8_t> pack = get_packet();
    if(pack.size())
        process_packet((uint8_t*)pack.data(), pack.size());
}
//----------------------------------------------------------------------------------------------------------------------
