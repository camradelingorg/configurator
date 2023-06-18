#include "fupdater.h"
//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
vector<uint8_t> FUpdater::get_packet()
{
	vector<uint8_t> packet;
	int len = InStream.size();
	if(len < 7)
	{
		return packet;
	}
	if(InStream[0] != MB_BROADCAST_ADDR)
	{
		state = ERROR;
		return packet;
	}
	int expectedLen=0;
	switch(InStream[1])
	{
	case 3:
	case 4:
	{
		expectedLen = 3 + InStream[2] + 2;
		break;
	}	
	case 6:
	case 16:
	{
		expectedLen = 8;
		break;
	}
	default:
	{
		state = ERROR;
		return packet;
	}	
	}
	if(len < expectedLen)
		return packet;
	uint16_t crc = MBCL->calc_crc(&InStream.data()[0],expectedLen-2);
	uint16_t receivedCRC = *(uint16_t*)&InStream.data()[expectedLen-2];
	if(crc != receivedCRC)
	{
		state = ERROR;
		return packet;
	}
	packet.insert(packet.end(),InStream.begin(),InStream.begin()+expectedLen);
	InStream.erase(InStream.begin(),InStream.begin()+expectedLen);
	return packet;
}
//----------------------------------------------------------------------------------------------------------------------
void FUpdater::process_packet(uint8_t* data, int len)
{
	vector<uint16_t> bytes;
	vector<uint8_t> packdata;
	timespec_t curStamp = {0,0};
	uint16_t crc=0;
	clock_gettime(CLOCK_MONOTONIC, &curStamp);
	switch(state)
	{
	case INITIAL_STATE:
		CHPLWRITELOG("unexpected packet, exiting...");
		exit(-1);
		break;
	case CHECK_BL_STATUS_SENT:
	{
		if(len*2 < MODBUS_03_DATASTART_IND+4)
		{
			CHPLWRITELOG("ERROR");
			state = ERROR;
			break;
		}
		BlStatus = (((uint16_t)data[MODBUS_03_DATASTART_IND] << 8) & 0xff00) | ((uint16_t)data[MODBUS_03_DATASTART_IND+1] & 0x00ff);
		if(BlStatus == FIRMWARE_RUNNING)
		{
			CHPLWRITELOG("running firmware - switching to bootloader");
			if(send_switch_to_bootloader())
				state = ERROR;
			else
			{
				lastState = state;
				state = SWITCH_TO_BL_SENT;
			}
		}
		else
		{
			CHPLWRITELOG("running bootloader - start uploading");
			if(send_one_more_block())
				state = ERROR;
		} 
		break;
	}
	case SWITCH_TO_BL_SENT:
	{
		lastState = state;
		if(send_sysreset())
			state = ERROR;
		else
		{
			sleep(1);
			CHPLWRITELOG("checking device status... ");
			packdata = MBCL->build_read_03(MB_BROADCAST_ADDR, MBHR_BOOTLOADER_STATUS,1);
			if(packdata.size() <= 0)
			{
				CHPLWRITELOG("error constructing modbus packet, exiting...");
				state = ERROR;
				break;
			}
			std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
			memcpy(buf->Data(),packdata.data(),packdata.size());
			shared_ptr<BasicChannel> schan = currentSession->ch.lock();
			if(!schan)
			{
				state = ERROR;
				break;
			}
			schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
			clock_gettime(CLOCK_MONOTONIC, &curStamp);
			packSentStamp = curStamp;
			lastState = state;
			state = CHECK_BL_STATUS_SENT;
		}
		break;
	}
	case FIRMWARE_BLOCK_SENT:
	{
		packdata = MBCL->build_read_03(MB_BROADCAST_ADDR, MBHR_COMMAND_STATUS,1);
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
			break;
		}
		schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
		packSentStamp = curStamp;
		lastState = state;
		state = CHECK_STATUS_SENT;
		break;
	}
	case FIRMWARE_WRITECRC_SENT:
	{
		packdata = MBCL->build_write_reg_06(MB_BROADCAST_ADDR, MBHR_BOOTLOADER_STATUS,BOOTLOADER_JUMP);
		if(packdata.size() <= 0)
		{
			CHPLWRITELOG("error constructing modbus packet, exiting...");
			state = ERROR;
		}
		std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
		memcpy(buf->Data(),packdata.data(),packdata.size());
		shared_ptr<BasicChannel> schan = currentSession->ch.lock();
		if(!schan)
			state = ERROR;
		schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
		packSentStamp = curStamp;
		lastState = state;
		state = FIRMWARE_START_SENT;
		break;
	}
	case FIRMWARE_START_SENT:
	{
		packdata = MBCL->build_read_03(MB_BROADCAST_ADDR, MBHR_COMMAND_STATUS,1);
		if(packdata.size() <= 0)
		{
			CHPLWRITELOG("error constructing modbus packet, exiting...");
			state = ERROR;
		}
		std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
		memcpy(buf->Data(),packdata.data(),packdata.size());
		shared_ptr<BasicChannel> schan = currentSession->ch.lock();
		if(!schan)
			state = ERROR;
		schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
		packSentStamp = curStamp;
		lastState = state;
		state = CHECK_STATUS_SENT;
		break;
	}
	case CHECK_STATUS_SENT:
	{
		switch(lastState)
		{
		case FIRMWARE_BLOCK_SENT:
		{
			if(len*2 < MODBUS_03_DATASTART_IND+4)
			{
				CHPLWRITELOG("ERROR");
				state = ERROR;
				break;
			}
			uint16_t status = (((uint16_t)data[MODBUS_03_DATASTART_IND] << 8) & 0xff00) | ((uint16_t)data[MODBUS_03_DATASTART_IND+1] & 0x00ff);
			//fprintf(stderr, "status = %04X ",status);
			if(status != COMMAND_STATUS_OK)
			{
				CHPLWRITELOG("ERROR");
				state = ERROR;
				break;
			}
			else
				CHPLWRITELOG("\rOK");
			if(firmwareNextAddr >= firmware->size())
			{
				CHPLWRITELOG("firmware upload finished, jumping to start address ... ");
				bytes.push_back(firmware->size());
				crc = MBCL->calc_crc(firmware->data(),firmware->size());
				bytes.push_back(crc);
				CHPLWRITELOG("firmware len = %04X",&bytes[0]);
				CHPLWRITELOG("firmware crc = %04X",&bytes[1]);
				packdata = MBCL->build_write_multreg_16(MB_BROADCAST_ADDR, MBHR_FIRMWARE_FULL_LEN, bytes);
				if(packdata.size() <= 0)
				{
					fprintf(stderr, "error constructing modbus packet, exiting...");
					state = ERROR;
				}
				std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
				memcpy(buf->Data(),packdata.data(),packdata.size());
				shared_ptr<BasicChannel> schan = currentSession->ch.lock();
				if(!schan)
				{
					state = ERROR;
					break;
				}
				schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
				packSentStamp = curStamp;
				lastState = state;
				state = FIRMWARE_WRITECRC_SENT;
			}
			else if(send_one_more_block())
				state = ERROR;
			break;
		}
		case FIRMWARE_START_SENT:
		{
			CHPLWRITELOG("firmware check status after start command shouldnt work, finishing...");
			exit(-1);
			break;
		}
		default:
			CHPLWRITELOG("unknown state, exiting...");
			exit(-1);	
		}  
		break;
	}
	default:
		CHPLWRITELOG("unknown state, exiting...");
		exit(-1);
		break;	
	}
}
//----------------------------------------------------------------------------------------------------------------------
int FUpdater::send_switch_to_bootloader()
{
	timespec_t curStamp = {0,0};
	clock_gettime(CLOCK_MONOTONIC, &curStamp);
	vector<uint8_t> packdata;
	packdata = MBCL->build_write_reg_06(MB_BROADCAST_ADDR, MBHR_BOOTLOADER_STATUS, BOOTLOADER_WAIT_30S);
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
	lastState = state;
	return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int FUpdater::send_sysreset()
{
	timespec_t curStamp = {0,0};
	clock_gettime(CLOCK_MONOTONIC, &curStamp);
	vector<uint8_t> packdata;
	CHPLWRITELOG("system reset requested...");
	packdata = MBCL->build_write_reg_06(MB_BROADCAST_ADDR, MBHR_REG_COMMAND, CMD_REBOOT);
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
	lastState = state;
	return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int FUpdater::send_one_more_block()
{
	CHPLWRITELOG("writing 0x80 bytes block, addr = %08X ... ",firmwareNextAddr);
	vector<uint16_t> data;
	vector<uint8_t> packdata;
	int blocklen = 0;
	uint16_t crc=0;
	timespec_t curStamp = {0,0};
	clock_gettime(CLOCK_MONOTONIC, &curStamp);
	//writing firmware relative addr
	data.push_back(firmwareNextAddr);
	//filling firmware block
	blocklen = ((firmwareNextAddr + MAX_FIRMWARE_BLOCK_SIZE) < firmware->size())?MAX_FIRMWARE_BLOCK_SIZE:(firmware->size()-firmwareNextAddr);
	for(int i = 0; i < MAX_FIRMWARE_BLOCK_SIZE; i+=2)
	{
		uint16_t dat=0;
		if(i == blocklen-1)
			dat = (uint16_t)(*(uint8_t*)&firmware->data()[firmwareNextAddr + i]) & 0x00ff;
		else if(i < blocklen-1)
			dat = *(uint16_t*)&firmware->data()[firmwareNextAddr + i];
		else
			dat = 0x0000;
		data.push_back(dat);
	}
	//write firmware block addr
	data.push_back(blocklen);
	//write block crc
	crc=MBCL->calc_crc(&firmware->data()[firmwareNextAddr],blocklen);
	data.push_back(crc);
	data.push_back(CMD_WRITE_FIRMWARE_BLOCK);
	packdata = MBCL->build_write_multreg_16(MB_BROADCAST_ADDR, MBHR_WRITE_FLASH_ADDR, data);
	if(packdata.size() <= 0)
	{
		CHPLWRITELOG("error constructing modbus packet, exiting...");
		return -1;
	}
	std::unique_ptr<MessageBuffer> buf(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
	memcpy(buf->Data(),packdata.data(),packdata.size());
	shared_ptr<BasicChannel> schan = currentSession->ch.lock();
	if(!schan)
	{
		return -1;
	}
	schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
	firmwareNextAddr += blocklen;
	packSentStamp = curStamp;
	lastState = state;
	state = FIRMWARE_BLOCK_SENT;
	return 0;
}
//----------------------------------------------------------------------------------------------------------------------
void FUpdater::thread_job()
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
	case INITIAL_STATE:
		CHPLWRITELOG("checking device status... ");
		packdata = MBCL->build_read_03(MB_BROADCAST_ADDR, MBHR_BOOTLOADER_STATUS,1);
		if(packdata.size() <= 0)
		{
			CHPLWRITELOG("error constructing modbus packet, exiting...");
			state = ERROR;
		}
		buf = std::unique_ptr<MessageBuffer>(new MessageBuffer(currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
		memcpy(buf->Data(),packdata.data(),packdata.size());
		schan = currentSession->ch.lock();
		if(!schan)
		{
			state = ERROR;
			break;
		}
		schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
		clock_gettime(CLOCK_MONOTONIC, &curStamp);
		packSentStamp = curStamp;
		lastState = state;
		state = CHECK_BL_STATUS_SENT;
		break;
	case CHECK_BL_STATUS_SENT:
	case SWITCH_TO_BL_SENT:
	case FIRMWARE_BLOCK_SENT:
	case FIRMWARE_WRITECRC_SENT:
	case FIRMWARE_START_SENT:
	case CHECK_STATUS_SENT:
		clock_gettime(CLOCK_MONOTONIC, &curStamp);
		diff = ((uint64_t)curStamp.tv_sec*1000 + (uint64_t)curStamp.tv_nsec/DECMILLION) - \
                    ((uint64_t)packSentStamp.tv_sec*1000 + (uint64_t)packSentStamp.tv_nsec/DECMILLION);
        if(diff > BLOCK_SEND_TIMEOUT_MS)
        {
        	CHPLWRITELOG("timeout error, exiting...");
			exit(-1);
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
