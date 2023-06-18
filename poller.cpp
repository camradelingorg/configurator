#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <fstream>
#include <iterator>
//----------------------------------------------------------------------------------------------------------------------
#include "modbus_client.h"
#include "chanlib_export.h"
#include "mpoller.h"
//----------------------------------------------------------------------------------------------------------------------
class SMPoller : public MPoller
{
public:
	SMPoller(mxml_node_t* cnf=nullptr):MPoller(cnf){}
	virtual ~SMPoller(){}
	virtual void process_packet(uint8_t* data, int len);
};
//----------------------------------------------------------------------------------------------------------------------
void SMPoller::process_packet(uint8_t* data, int len)
{
	packSent=0;
	switch(data[1])
	{
	case 3:
	case 4:
	{
		if(data[2] == 2)
		{
			uint16_t reg = (((uint16_t)data[MODBUS_03_DATASTART_IND] << 8) & 0xff00) | ((uint16_t)data[MODBUS_03_DATASTART_IND+1] & 0x00ff);
			fprintf(stderr, "reg value: %04X\n",reg);
		}
		break;
	}	
	case 6:
	case 16:
	{
		fprintf(stderr, "write: OK");
		break;
	}
	default:
	{
		break;
	}	
	}
	return;
}
//----------------------------------------------------------------------------------------------------------------------
const char* defaultConf = (char*)"./config.xml";
shared_ptr<SMPoller> smpoll;
//----------------------------------------------------------------------------------------------------------------------
int cmdparse(string request)
{
#define CMDFAIL(x)  request = "";fprintf(stderr,x);return -1;
    if(request.length() <= 0)
    {
        request = "";return -1;
    }
    vector<string> args;
    string delimiter = " ";
    size_t pos = 0;
    while ((pos = request.find(delimiter)) != std::string::npos)
    {
        args.push_back(request.substr(0, pos));
        request.erase(0, pos + delimiter.length());
    }
    if(request.length() > 0)
        args.push_back(request);
    if(args.size() < 2)
    {
        CMDFAIL("command invalid. Too few arguments.\n");
    }
    if(args.size() > 3)
    {
        CMDFAIL("command invalid. Too many arguments.\n");
    }
    if(args[0] == "r")
    {
        if(args.size() > 2)
        {
            CMDFAIL("command invalid. Too many arguments.\n");
        }
        uint16_t addr=0;
        errno = 0;
        if(args[1].length() > 2 && args[1][0] == '0' && args[1][0] == 'x')
            addr = strtoul(args[1].c_str(),NULL,16);
        else
            addr = strtoul(args[1].c_str(),NULL,10);
        if(errno)
        {
            CMDFAIL("command invalid. register number invalid.\n");
        }
        vector<uint8_t> packdata = smpoll->MBCL->build_read_03(MB_BROADCAST_ADDR, addr,1);
        std::unique_ptr<MessageBuffer> buf(new MessageBuffer(smpoll->currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
        memcpy(buf->Data(),packdata.data(),packdata.size());
        shared_ptr<BasicChannel> schan = smpoll->currentSession->ch.lock();
        if(!schan)
        {
            CMDFAIL("no active session.\n");
        }
        clock_gettime(CLOCK_MONOTONIC,&smpoll->packSentStamp);
        schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
    }
    else if(args[0] == "w")
    {
        if(args.size() < 3)
        {
            CMDFAIL("command invalid. Too few arguments.\n");
        }
        uint16_t addr=0;
        errno = 0;
        if(args[1].length() > 2 && args[1][0] == '0' && args[1][0] == 'x')
            addr = strtoul(args[1].c_str(),NULL,16);
        else
            addr = strtoul(args[1].c_str(),NULL,10);
        if(errno)
        {
            CMDFAIL("command invalid. register number invalid.\n");
        }
        uint16_t val=0;
        errno = 0;
        if(args[2].length() > 2 && args[2][0] == '0' && args[2][0] == 'x')
            val = strtoul(args[2].c_str(),NULL,16);
        else
            val = strtoul(args[2].c_str(),NULL,10);
        if(errno)
        {
            CMDFAIL("command invalid. register value invalid.\n");
        }
        vector<uint8_t> packdata = smpoll->MBCL->build_write_reg_06(MB_BROADCAST_ADDR,addr,val);
        std::unique_ptr<MessageBuffer> buf(new MessageBuffer(smpoll->currentSession->fd, packdata.size(), CHAN_DATA_PACKET));
        memcpy(buf->Data(),packdata.data(),packdata.size());
        shared_ptr<BasicChannel> schan = smpoll->currentSession->ch.lock();
        if(!schan)
        {
            CMDFAIL("no active session.\n");
        }
        clock_gettime(CLOCK_MONOTONIC,&smpoll->packSentStamp);
        schan->send_message_buffer(&schan->outQueue, std::move(buf), true);
    }
    else
    {
        CMDFAIL("command invalid. Wrong command token.\n");
    }
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    string configfile;
    int option_index = 0;
    int c;
    static struct option long_options[] = {
            {"config",  required_argument,NULL,0},
            {NULL,    0,                 NULL,  0 }
    };
    while((c = getopt_long(argc, argv, "i:c:",long_options,&option_index)) != -1)
    {
        switch(c)
        {
            case 0:
                if(option_index == 0)
                {
                    configfile = string(optarg);
                }
                break;
            case 'c':
                configfile = string(optarg);
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
    //fprintf(stderr, "configuration file = %s\n",configfile.c_str());
    bool confok = 1;
    FILE* fpconf=nullptr;
    mxml_node_t* tree = nullptr;
    if ((fpconf = fopen(configfile.c_str(), "r")) != NULL)        // открыть файл с конфигурацией в формате XML
    {
        tree = mxmlLoadFile (nullptr, fpconf, MXML_NO_CALLBACK);       // считать конфигурацию
        fclose(fpconf);
        if (tree == nullptr)
        {
            fprintf(stderr, "config file invalid\n");
            confok = 0;
            return -1;
        }
    }
    else
    {
        fprintf(stderr, "cant open config file\n");
        confok = 0;
        return -1;
    }
    smpoll = shared_ptr<SMPoller>(new SMPoller(tree));
    smpoll->init_module();
    string cmd="";
    fprintf(stderr, "inited\n");
    while(!smpoll->stop)
    {
        if(!smpoll->currentSession)
            continue;
        std::getline(cin,cmd);
        cmdparse(cmd);
    }
	return 0;
}
//----------------------------------------------------------------------------------------------------------------------