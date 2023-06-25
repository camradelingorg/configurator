#ifndef MPOLLER_H
#define MPOLLER_H
//----------------------------------------------------------------------------------------------------------------------
#include "chanlib_export.h"
#include "mxml.h"
#include "programthread.h"
#include "modbus_client.h"
#include "modbus_config.h"
//----------------------------------------------------------------------------------------------------------------------
#define PACKET_SEND_TIMEOUT_MS 		500
//----------------------------------------------------------------------------------------------------------------------
#define CHPLWRITELOG(format,...) {if(CHPL->logger) CHPL->logger->write(CHPL->DebugValue,format,##__VA_ARGS__);}
//----------------------------------------------------------------------------------------------------------------------
typedef struct _Session
{
	weak_ptr<BasicChannel> ch;
	uint32_t fd = 0;
	uint8_t	 deviceOnline = 0;
	timespec_t confirmStamp={0,0};
	timespec_t kASpan = {0,0};
	timespec_t kAStamp = {0,0};
	timespec_t kAStampR = {0,0};
	bool zipflag=0;
	uint32_t InSeq=0;
	uint32_t OutSeq=0;
	string chanaddr="";
}Session;
//----------------------------------------------------------------------------------------------------------------------
class MPoller : public ProgramThread
{
public:
	MPoller(mxml_node_t* cnf=nullptr):config(cnf){}
	virtual ~MPoller(){}
	virtual void init_module();
	virtual void thread_job();
	virtual void process_channel(weak_ptr<BasicChannel> chan);
	virtual vector<uint8_t> get_packet();
	virtual void process_packet(uint8_t* data, int len);
	vector<uint8_t> InStream;
	shared_ptr<ModbusClient> MBCL;
	shared_ptr<ChanPool> CHPL;
	vector<Session> sessionsActive;
	Session* currentSession=nullptr;
	mxml_node_t* config=nullptr;
	timespec_t packSentStamp={0,0};
	bool packSent=0;
};
//----------------------------------------------------------------------------------------------------------------------
#endif/*MPOLLER_H*/