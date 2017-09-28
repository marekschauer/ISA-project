#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <unistd.h>
using namespace std;

void printHelp() {
	std::cout << "This will be help, it's gonna be awesome" << endl;
}

/**
 * @brief      Function for getting arguments, when no port is provided in arguments, it is set to string 6677
 *
 * @param[in]  argumentsCount  The arguments count
 * @param      arguments       The arguments
 * @param      rootFolder      The root folder
 * @param      port            The port
 */
void handleArguments(int argumentsCount, char** arguments, std::string &authFile, bool &	clearPass, int &	port, std::string &maildirPath, bool &	reset) {
    int ch;
    
    while ((ch = getopt(argumentsCount, arguments, "ha:cp:d:r")) != -1) {
        switch (ch) {
            case 'h':
                printHelp();
                break;
            case 'a':
                authFile = optarg;
                break;
            case 'c':
            	clearPass = true;
            	break;
            case 'p':
            	port = strtol(optarg, NULL, 0);
            	break;
            case 'd':
            	maildirPath = optarg;
            	break;
            case 'r':
            	reset = true;
            	break;
            default:
            	break;
        }
    }
}


int main(int argc, char *argv[])
{
    // handleArguments(argc, argv, ip_address, username);
    std::string authFile("");
    bool clearPass = false;
    int port = 0;
    std::string maildirPath("");
    bool reset = false;
    handleArguments(argc, argv, authFile, clearPass, port, maildirPath, reset);
	std::cout << "Hello world!\r\n";
	std::cout << "authFile " << authFile << endl;
	std::cout << "clearPass " << clearPass << endl;
	std::cout << "port " << port << endl;
	std::cout << "maildirPath " << maildirPath << endl;
	std::cout << "reset " << reset << endl;
	
	return 0;
}
