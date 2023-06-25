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
#include "configurator.h"
//----------------------------------------------------------------------------------------------------------------------
const char* defaultConf = (char*)"./config.xml";
//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    string filename;
    string configfile;
    int option_index = 0;
    int c;
    int reboot=0;
    int factory_reset=0;
    static struct option long_options[] = {
            {"input",     required_argument,NULL,0},
            {"config",  required_argument,NULL,0},
            {"reboot",  no_argument,NULL,0},
            {"reset",  no_argument,NULL,0},
            {NULL,    0,                 NULL,  0 }
    };
    while((c = getopt_long(argc, argv, "i:c:rf",long_options,&option_index)) != -1)
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
                else if(option_index == 2)
                {
                    reboot=1;
                }
                else if(option_index == 3)
                {
                    factory_reset=1;
                }
                break;
            case 'i':
                filename = string(optarg);
                break;
            case 'c':
                configfile = string(optarg);
                break;
            case 'r':
                reboot=1;
                break;
            case 'f':
                factory_reset=1;
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
    shared_ptr<Configurator> mconf = shared_ptr<Configurator>(new Configurator(tree));
    memset(mconf->mmap->MBHR_COPY,0,MBHR_SETTINGS_SIZE*2);
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
        map<string,parser_func>::iterator pit = mconf->mmap->pmap.find(it.key());
        if(pit != mconf->mmap->pmap.end())
        {
            if(pit->second(it.value()))
            {
                cout << it.key() << ": parsing failed" << endl;
                exit(-1);
            }
        }
    }
    
    mconf->init_module();
    while(!mconf->CHPL->inited);
    sleep(1);
    //from now we issue commands from this thread and reply is processed in Configurator thread
    if(mconf->write_reg(MBHR_LOG_ON, 0))
    {
        fprintf(stderr, "failed to write log stop cmd");
        return -1;
    }
    //mconf->read_reg(1, 1);
    //fprintf(stderr,"reg 1: %04X\n",mconf->mmap->MBHR_COPY[1]);
    if(reboot)
    {
        //if reboot needed
        int res = mconf->write_reg(MBHR_REG_COMMAND,CMD_REBOOT);
        if(res)
        {
            fprintf(stderr, "reboot fail\n");
            return -1;
        }
        else
        {
            fprintf(stderr, "reboot successfully\n");
        }
    }
    else if(factory_reset)
    {
        //if factory_reset needed
        int res = mconf->write_reg(MBHR_REG_COMMAND,CMD_ERASE_SETTINGS);
        if(res)
        {
            fprintf(stderr, "factory reset fail\n");
            return -1;
        }
        else
        {
            fprintf(stderr, "factory reset successful\n");
        }
    }
    else
    {
        int res = mconf->write_configuration();
        if(res)
        {
            fprintf(stderr, "configuration write fail\n");
            return -1;
        }
        else
        {
            fprintf(stderr, "configuration write successful\n");
        }
    }
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------
