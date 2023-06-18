#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <fstream>
#include <iterator>
//----------------------------------------------------------------------------------------------------------------------
#include "modbus_client.h"
#include "chanlib_export.h"
#include "fupdater.h"
//----------------------------------------------------------------------------------------------------------------------
const char* defaultConf = (char*)"./config.xml";
//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
	string filename;
    string configfile;
    int option_index = 0;
    int c;
    static struct option long_options[] = {
            {"input",     required_argument,NULL,0},
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
            case '?':
                fprintf(stderr, "unknown option: %c\n", option_index);
                break;
        }
    }
    if(filename == "")
    {
        fprintf(stderr, "no firmware file provided\n");
        return -1;
    }
	if(configfile == "")
    {
        filename = string(defaultConf);
    }
    fprintf(stderr, "configuration file = %s\n",configfile.c_str());
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
    fprintf(stderr, "config file valid\n");
    std::ifstream input( filename, std::ios::binary );

    // copies all data into buffer
    std::vector<uint8_t> firmware(std::istreambuf_iterator<char>(input), {});
    if(firmware.size() <= 0)
        return -1;
    fprintf(stderr, "firmware file read, data size = %08X\n",firmware.size());
    shared_ptr<FUpdater> fupd = shared_ptr<FUpdater>(new FUpdater(tree));
    fupd->firmware = &firmware;
    fupd->init_module();
    while(!fupd->stop);
	return 0;
}
//----------------------------------------------------------------------------------------------------------------------