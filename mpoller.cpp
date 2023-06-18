#include "mpoller.h"
//----------------------------------------------------------------------------------------------------------------------
void MPoller::init_module()
{
	//CHPL = shared_ptr<ChanPool>(new ChanPool(new ChannelLib::Logger));
	CHPL = shared_ptr<ChanPool>(new ChanPool());
    CHPL->chp = CHPL;
	MBCL = shared_ptr<ModbusClient>(new ModbusClient);
	mxml_node_t* loggernode = mxmlFindElement(config, config, "Logger", NULL, NULL, MXML_DESCEND);
    if(loggernode)
    {
        const char* ext = mxmlGetText(loggernode,NULL);
        if(ext && string(ext) == "true") CHPL->logger = new ChannelLib::Logger;
    }
	CHPL->init(config);
	if(CHPL->allChan.size() > 1)
	{
		fprintf(stderr, "multiple channels in config, closing...\n");
		exit(-1);
	}
	for(int i = 0; i < CHPL->allChan.size(); i++)
    {
		weak_ptr<BasicChannel> chan = CHPL->allChan.at(i);
		shared_ptr<BasicChannel> schan = chan.lock();
		add_pollable_handler(schan->inCmdQueue.fd(), EPOLLIN, &MPoller::process_channel, this, chan);
        add_pollable_handler(schan->inQueue.fd(), EPOLLIN, &MPoller::process_channel, this, chan);
    }
    ProgramThread::init_module();
}
//----------------------------------------------------------------------------------------------------------------------
void MPoller::process_channel(weak_ptr<BasicChannel> chan)
{
	shared_ptr<BasicChannel> schan = chan.lock();
	if(!schan)
		return;
	std::unique_ptr<MessageBuffer> packet;
	while((packet = schan->inCmdQueue.pop()) && !stop)
	{
		enum MessageType packetType = packet->Type();
		if (packetType == CHAN_OPEN_PACKET && sessionsActive.size() == 0)
		{
		    Session ses;
			ses.fd = packet->getfd();
			CHPLWRITELOG("connection established\n");
			ses.ch = schan;
			ses.deviceOnline = 0;
			if(packet->getChanAddr() != "")
				ses.chanaddr = packet->getChanAddr();
			//здесь надо послать инит мессадж
			sessionsActive.push_back(ses);
			currentSession = &sessionsActive.at(sessionsActive.size()-1);
		}
		else if (packetType == CHAN_CLOSE_PACKET)
		{
			for (int j = 0; j < sessionsActive.size(); j++)
			{
				if (sessionsActive.at(j).fd == packet->getfd())
				{
					sessionsActive.erase(sessionsActive.begin() + j);
					break;
				}
			}
			currentSession = nullptr;
			CHPLWRITELOG("command channel closed unexpectedly, exiting...\n");
			exit(-1);
		}
	}
    while((packet = schan->inQueue.pop()) && !stop)
	{
		enum MessageType packetType = packet->Type();
    	currentSession = nullptr;
        if (packetType == CHAN_DATA_PACKET)
        {//для других зарезервируем это же значение
        	for (int j = 0; j < sessionsActive.size(); j++)
            {
                if (sessionsActive.at(j).fd == packet->getfd())
                {
                    currentSession = &sessionsActive[j];
                    currentSession->InSeq = packet->seqnum;
                    InStream.insert(InStream.end(),packet->Data(),packet->Data()+packet->Length());
                    break;
                }
            }
        }
        else
        	CHPLWRITELOG("Unknown data packet type %d\n", packetType);
	}
}
//----------------------------------------------------------------------------------------------------------------------
vector<uint8_t> MPoller::get_packet()
{
	vector<uint8_t> packet;
	int len = InStream.size();
	if(len < 7)
	{
		return packet;
	}
	if(InStream[0] != MB_BROADCAST_ADDR)
	{
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
		return packet;
	}	
	}
	if(len < expectedLen)
		return packet;
	uint16_t crc = MBCL->calc_crc(&InStream.data()[0],expectedLen-2);
	uint16_t receivedCRC = *(uint16_t*)&InStream.data()[expectedLen-2];
	if(crc != receivedCRC)
	{
		return packet;
	}
	packet.insert(packet.end(),InStream.begin(),InStream.begin()+expectedLen);
	InStream.erase(InStream.begin(),InStream.begin()+expectedLen);
	return packet;
}
//----------------------------------------------------------------------------------------------------------------------
void MPoller::process_packet(uint8_t* data, int len)
{
	packSent=0;
	return;
}
//----------------------------------------------------------------------------------------------------------------------
void MPoller::thread_job()
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
	if(packSent)
	{
		clock_gettime(CLOCK_MONOTONIC, &curStamp);
		diff = ((uint64_t)curStamp.tv_sec*1000 + (uint64_t)curStamp.tv_nsec/DECMILLION) - \
                    ((uint64_t)packSentStamp.tv_sec*1000 + (uint64_t)packSentStamp.tv_nsec/DECMILLION);
        if(diff > PACKET_SEND_TIMEOUT_MS)
        {
        	CHPLWRITELOG("timeout error, exiting...\n");
        	stop=1;
			exit(-1);
        }
	}
}
//----------------------------------------------------------------------------------------------------------------------
