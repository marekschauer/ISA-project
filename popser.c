#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
using namespace std;

/**
 * @brief      Function for printing help and exiting whole program
 */
void printHelp() {		//TODO
	std::cout << "This will be help, it's gonna be awesome" << endl;
	exit(EXIT_SUCCESS);
}

/**
 * @brief      Checks, whether exists the file of given name.
 *
 * @param[in]  strFileName  The string file name
 *
 * @return     1 when file eists, 0 otherwise
 */
int fileExists(std::string strFileName) {
	if( access( strFileName.c_str(), F_OK ) != -1 ) {
		return 1;
	}
	return 0;
}

/**
 * @brief      Checks, whether exists the folder of given name.
 *
 * @param[in]  strFileName  The string name of folder
 *
 * @return     1 when folder eists, 0 otherwise
 */
int directoryExists(std::string strFolderName) {       
	const char* folderr;
	folderr = strFolderName.c_str();
	struct stat sb;

	if (stat(folderr, &sb) == 0 && S_ISDIR(sb.st_mode))
	{
		return 1;
	}
	return 0;
}


/**
 * @brief      Function for getting arguments, exits when there's something wrong with arguments.
 * 			   When this function is called and program continues, arguments passed to program are OK.
 *
 * @param[in]  argumentsCount  The arguments count
 * @param      arguments       The arguments
 * @param      rootFolder      The root folder
 * @param      port            The port
 */
void handleArguments(int argumentsCount, char** arguments, std::string &authFile, bool &	clearPass, int &	port, std::string &maildirPath, bool &	reset) {
	int ch;
	port = -1;
    authFile = "";				// TODO - ako sa ma defaultne volat autorizacny subor?
    maildirPath = "";
    while ((ch = getopt(argumentsCount, arguments, "ha:cp:d:r")) != -1) {
    	switch (ch) {
    		case 'h':
    		printHelp();
    		break;
    		case 'a':
    		authFile = optarg;
    		break;
    		case 'p':
    		port = strtol(optarg, NULL, 0);
    		break;
    		case 'd':
    		maildirPath = optarg;
    		break;
    		case 'c':
    		clearPass = true;
    		break;
    		case 'r':
    		reset = true;
    		break;
    		default:
    		break;
    	}
    }

    if (reset && (port == -1 || authFile == "" || maildirPath == "") && !(port == -1 && authFile == "" && maildirPath == "")) {
    	if (port == -1) {
    		fprintf(stderr,"ERROR: Argument -p (port) is not given\n");
    	} else if (authFile == "") {
    		fprintf(stderr,"ERROR: Path to authorisation file through the parameter -a is not given\n");
    	} else if (maildirPath == "") {
    		fprintf(stderr,"ERROR: Path to Maildir through the parameter -d is not given\n");
    	}
    	exit(EXIT_FAILURE);	
    } else if (!reset && (port == -1 || authFile == "" || maildirPath == "")) {
    	fprintf(stderr,"ERROR: Wrong parrameters, use --help for further information about parameters usage\n");
    	exit(EXIT_FAILURE);
    }

    if (authFile != "" && fileExists(authFile) == 0) {
    	fprintf(stderr,"ERROR: authorisation file not found\n");
    	exit(EXIT_FAILURE);
    }

    if (maildirPath != "" && directoryExists(maildirPath) == 0) {
    	fprintf(stderr,"ERROR: Maildir not found\n");
    	exit(EXIT_FAILURE);	
    }
}

/**
 * @brief      Gets the value of username from given string containing "...username = name..."
  *
 * @param[in]  authFileContent  The authorisation file content
 *
 * @return     The value between "username = " and the end of line
 */
std::string getUsername(std::string authFileContent) {
	return authFileContent.substr(authFileContent.find("username = ") + 11, authFileContent.length()).substr(0, authFileContent.substr(authFileContent.find("username = ") + 11, authFileContent.length()).find("\n"));
}


/**
 * @brief      Gets the value of username from given string containing "...username = name..."
  *
 * @param[in]  authFileContent  The authorisation file content
 *
 * @return     The value between "username = " and the end of line
 */
std::string getPassword(std::string authFileContent) {
	return authFileContent.substr(authFileContent.find("password = ") + 11, authFileContent.length()).substr(0, authFileContent.substr(authFileContent.find("password = ") + 11, authFileContent.length()).find("\n"));
}

/**
 * @brief      Gets the content of file specified by name or path.
 *
 * @param[in]  fileName  The file name or path to the file
 *
 * @return     The file content represented.
 */
std::string getFileContent(std::string fileName) {
	FILE *fp;
	int c;
	std::string content;
	fp = fopen(fileName.c_str(),"r");
	if (fp != NULL) {
		while(1) {
			c = fgetc(fp);
			std::cout << c << endl;
			if( feof(fp) )
			{ 
				break ;
			}
			content += (char) c;
		}
	}
	fclose(fp);
	return content;
}

int main(int argc, char *argv[])
{
	std::string authFile("");
	bool clearPass = false;
	int port = 0;
	std::string maildirPath("");
	bool reset = false;
	handleArguments(argc, argv, authFile, clearPass, port, maildirPath, reset);
	
	std::string authFileContent = getFileContent(authFile);
	std::cout << "--->" << getUsername(authFileContent) << "<---" << endl;
	std::cout << "--->" << getPassword(authFileContent) << "<---";
	
	return 0;
}
