#ifndef FUPDATER_H
#define FUPDATER_H
//----------------------------------------------------------------------------------------------------------------------
#include "chanlib_export.h"
#include "programthread.h"
#include "modbus_client.h"
#include "modbus_config.h"
#include "mpoller.h"
//----------------------------------------------------------------------------------------------------------------------
#define BLOCK_SEND_TIMEOUT_MS 		PACKET_SEND_TIMEOUT_MS
#define FIRMWARE_CHECK_TIMEOUT_MS 	1500
//----------------------------------------------------------------------------------------------------------------------
enum WorkState
{
	INITIAL_STATE = 0,
	CHECK_BL_STATUS_SENT,
	SWITCH_TO_BL_SENT,
	FIRMWARE_BLOCK_SENT,
	FIRMWARE_WRITECRC_SENT,
	FIRMWARE_START_SENT,
	CHECK_STATUS_SENT,
	ERROR
};
//----------------------------------------------------------------------------------------------------------------------
class FUpdater : public MPoller
{
public:
	FUpdater(mxml_node_t* cnf=nullptr):MPoller(cnf){}
	virtual ~FUpdater(){}
	//virtual void init_module();
	virtual void thread_job();
	//virtual void process_channel(weak_ptr<BasicChannel> chan);
	virtual vector<uint8_t> get_packet();
	virtual void process_packet(uint8_t* data, int len);
	int send_one_more_block();
	int send_switch_to_bootloader();
	int send_sysreset();
	std::vector<uint8_t>* firmware=nullptr;
	uint32_t firmwareNextAddr=0;
	WorkState state = INITIAL_STATE;
	WorkState lastState = INITIAL_STATE;
	uint16_t BlStatus=FIRMWARE_RUNNING;
};
//----------------------------------------------------------------------------------------------------------------------
#endif/*FUPDATER_H*/