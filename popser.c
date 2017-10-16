#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <netdb.h>
#include <err.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctime>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <dirent.h>
#include "md5.h"

using namespace std;

#define BUFFER	(512)
#ifndef MAX_BUFFER_SIZE
#define MAX_BUFFER_SIZE (512)
#endif
#define QUEUE	(2)

typedef enum {
	cmd_user,
	cmd_pass,
	cmd_apop,
	cmd_list,
	cmd_stat,
	cmd_retr,
	cmd_quit,
	cmd_unknown
} CommandName;


typedef enum {
	state_USER_REQUIRED,
	state_PASSWORD_REQUIRED,
	state_APOP_REQUIRED,
	state_TRANSACTION,
	state_UPDATE
} SessionState;


typedef struct programParameters {
	std::string authFile;
	bool clearPass;
	int port;
	std::string maildirPath;
	bool reset;
	std::string username;
	std::string password;
} Parameters;

typedef struct argumentsForThreadStructure {
	int acceptedSockDes;
	SessionState sessionState;
	std::string timestamp;
	Parameters* params;
} ArgsToThread;

typedef struct cmd {
	CommandName name;
	std::string firstArg;
	std::string secondArg;
} Command;

typedef struct emails {
	std::string fileName;
	uint size;

} EmailsStruct;


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


    if (reset && argumentsCount > 2) {
    	std::cout << "SOM TU" << endl;
    	if (port == -1) {
    		fprintf(stderr,"ERROR: Argument -p (port) is not given\n");
    	} else if (authFile == "") {
    		fprintf(stderr,"ERROR: Path to authorisation file through the parameter -a is not given\n");
    	} else if (maildirPath == "") {
    		fprintf(stderr,"ERROR: Path to Maildir through the parameter -d is not given\n");
    	}
    	exit(EXIT_FAILURE);	
    } else if ((port == -1 || authFile == "" || maildirPath == "") && !reset) {
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

/**
 * @brief      Function for getting hostname represented as std::string
 *
 * @return     The host name string.
 */
std::string getHostNameStr() {
	char hostname[100];
	gethostname(hostname, 100);
	return hostname;
}

/**
 * @brief      Uppercases given text
 *
 * @param[in]  toBeUppercased  To be uppercased
 *
 * @return     The uppercased text
 */
std::string uppercase(std::string toBeUppercased) {
	std::string toBeReturned("");
	
	for (unsigned int i = 0; i < toBeUppercased.length(); ++i) {
		toBeReturned += (char) toupper(toBeUppercased.at(i));
	}
	return toBeReturned;
}

/**
 * @brief      Function for getting current timestamp (number of seconds passed from 1.1.1970)
 * 
 * @author     Grayson Koonce (https://graysonkoonce.com/getting-a-unix-timestamp-in-cpp/)
 * @return     Current timestamp in unix format
 */
long int unix_timestamp() {
	time_t t = std::time(0);
	long int now = static_cast<long int> (t);
	return now;
}

/**
 * @brief      Gets the timestamp for initial message
 *
 * @return     The timestamp.
 */
std::string getTimestamp() {
	return "<"+std::to_string(getpid())+"."+std::to_string(unix_timestamp())+"@"+getHostNameStr()+">";
}

/**
 * @brief      Lists all files of given directory except "." and ".."
 *
 * @param[in]  name  The path to the directory to be read
 * @param      v     Vector of strings where the file names should be saved in
 * @author     http://www.martinbroadhurst.com/list-the-files-in-a-directory-in-c.html
 */
void readDirectory(const std::string& name, std::vector<std::string>& v) {
	if (directoryExists(name) != 1) {
		// TODO - sprav co sa stane ak sa nenajde dany adresar, toto je asi dumb
		return;
	}
	DIR* dirp = opendir(name.c_str());
	struct dirent * dp;
	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, "..") != 0 && strcmp(dp->d_name, ".") != 0) {
			v.push_back(dp->d_name);
		}
	}
	closedir(dirp);
}

/**
 * @brief      Splits a filename.
 *
 * @param[in]  str   The string
 */
std::string SplitFilename (std::string str) {
	return str.substr(str.find_last_of("/\\")+1);
}

/**
 * @brief      Gets the file size.
 *
 * @return     The file size.
 */
std::ifstream::pos_type filesize(const char* filename) {
	std::ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
	return in.tellg(); 
}

/**
 * @brief      Moves a specified file to specified destination
 *
 * @param[in]  file  The file to be moved
 * @param[in]  dest  The destination of the moved file
 *
 * @return     int   0 if everything proceeded correctly
 *                   1 if file to be moved cannot be open
 *                   2 if the file to be moved does not exist
 *                   3 if the new file file cannot be open
 *                   4 if there is already the file with the same name on destination
 *                   5 if there is error while deleting the file to be moved
 */
int moveFile(std::string file, std::string dest) {
	std::string fileName = SplitFilename(file);
	std::vector<char> fileContent;
   //TODO - it would be nice to check whether the new file has the same size as the new one has
   // At first, reading the file and pushing it to the vector of chars.
	if (fileExists(file)) {
		if (fileExists(file)) {
			FILE *fp;
			int c;
			fp = fopen(file.c_str(),"rb");
			if (fp != NULL) {
				while(1) {
					c = fgetc(fp);
					if( feof(fp) ) { 
						break ;
					}
               //Plnenie vektoru:
					fileContent.push_back(c);
				}
			} else {
            // Nepodarilo sa mi otvorit file
				return 1;
			}
			fclose(fp);
		}
	} else {
      // Subor neexistuje
		return 2;
	}
   // Making string containing the content of file to be copied
	std::string fileContentStr(fileContent.begin(), fileContent.end());

   // Making new file and putting in the content of the old one
	std::string pathToNewFile("");
	pathToNewFile.append(dest).append("/").append(fileName);

	if (!fileExists(pathToNewFile)) {
		FILE* fileHandle;
		fileHandle = fopen(pathToNewFile.c_str(),"wb");
		if (fileHandle != NULL) {
			fwrite(fileContentStr.c_str(), sizeof(char), fileContentStr.size(), fileHandle);
		} else {
         // Nepodarilo sa mi otvorit subor
			return 3;
		}
		fclose(fileHandle);
	} else {
      // Nevytvoril som, lebo uz existuje
		return 4;
	}

	if (remove(file.c_str()) != 0) {
      // vznikol error pri mazani
		return 5;
	}
	return 0;
}

/**
 * @brief      TODO
 */
void userAuthenticated() {
	std::vector<EmailsStruct> emails;
	std::vector<std::string> fileNames;
	int mvRet;

	readDirectory("Maildir/new", fileNames);				// reading new emails

	for (int i = 0; i < fileNames.size(); ++i) {
		// filling the structure containing the emails
		std::cout << fileNames.at(i) << std::endl;
		if ((mvRet = moveFile("Maildir/new/" + fileNames.at(i), "Maildir/cur/")) != 0) {
			std::cout << "Some problem with deleting file " << fileNames.at(i) << " errcode(" << mvRet << ")";
		}
		EmailsStruct tmp;
		tmp.fileName = fileNames.at(i);
		std::string fileNameWithPath("Maildir/cur/");
		fileNameWithPath.append(fileNames.at(i));
		tmp.size = filesize(fileNameWithPath.c_str());
		emails.push_back(tmp);
		std::cout << "Email " << emails.at(i).fileName << " has size " << emails.at(i).size << "B" << std::endl;
		emails.at(i);
	}

	std::cout << "-----------------" << std::endl;
	for (int i = 0; i < emails.size(); ++i) {
		std::cout << emails.at(i).fileName << std::endl;
	}
	std::cout << "-----------------" << std::endl;

}

bool checkForSpaceAfterCommand(std::string message) {
	return (message.substr(4,1) == " ") ? true : false;
}

Command getCommand(std::string message) {
	std::string usersCommand = uppercase(message.substr(0,4)); // TODO - osetri, ak pride prikaz s dlzkou menej ako 4
	                                                           // TODO - moze byt aj prikaz TOP, ten ma len 3 znaky	
	Command toBeReturned;
	toBeReturned.name = cmd_unknown;
	toBeReturned.firstArg.clear();
	toBeReturned.secondArg.clear();

	if (usersCommand == "USER" && checkForSpaceAfterCommand(message)) {
		toBeReturned.name 		= cmd_user;
		toBeReturned.firstArg 	= message.substr(5, message.length()-7);
	} else if (usersCommand == "PASS" && checkForSpaceAfterCommand(message)) {
		toBeReturned.name 		= cmd_pass;
		toBeReturned.firstArg 	= message.substr(5, message.length()-7);
	} else if (usersCommand == "APOP" && checkForSpaceAfterCommand(message)) {
		std::string arguments 	= message.substr(5, message.length()-7);
		toBeReturned.name 		= cmd_apop;
		toBeReturned.firstArg 	= arguments.substr(0, arguments.find(" "));
		toBeReturned.secondArg 	= arguments.substr(arguments.find(" ")+1, arguments.length()-arguments.find(" "));
	} else if (usersCommand == "QUIT") {
		toBeReturned.name 		= cmd_quit;
	}
	
	return toBeReturned;
}

std::string process_message(std::string message, ssize_t n, argumentsForThreadStructure* threadArgs) {
	std::cout << getCommand(message).name << std::endl;
	std::string toBeReturned("");
	Command actualCommand = getCommand(message);

	switch(threadArgs->sessionState) {
		case state_USER_REQUIRED:
			if (getCommand(message).name == cmd_user) {
				if (threadArgs->params->username == actualCommand.firstArg) {
					toBeReturned = "+OK\r\n";
					threadArgs->sessionState = state_PASSWORD_REQUIRED;
				} else {
						// Autentizacia zlyhala, vraciam error
					toBeReturned = "-ERR\r\n";
						//TODO	co teraz? Mam daneho usera odpojit? Alebo len ostat v stave, ktory vyzaduje meno?
						//		momentalne ostavam v stave vyzadujucom meno
				}
			} else if (getCommand(message).name == cmd_apop) {
				if (threadArgs->params->username == actualCommand.firstArg) {
						// Now user is ok, let's test hash equality
					std::string myHash = md5(threadArgs->timestamp + threadArgs->params->password);
					if (myHash == actualCommand.secondArg) {
						toBeReturned = "+OK\r\n";
						threadArgs->sessionState = state_TRANSACTION;
					} else {
						toBeReturned = "-ERR\r\n";
					}
				} else {
					toBeReturned = "-ERR\r\n";
				}
			} else {
					// pripad, ze som v stave USER_REQUIRED a neprisiel mi prikaz user ani apop
				toBeReturned = "-ERR\r\n";
			}
			break;
		case state_PASSWORD_REQUIRED:
			if (getCommand(message).name == state_PASSWORD_REQUIRED) {
				if (threadArgs->params->password == actualCommand.firstArg) {
					toBeReturned = "+OK\r\n";
					threadArgs->sessionState = state_TRANSACTION;
					userAuthenticated();
				} else {
					threadArgs->sessionState = state_USER_REQUIRED;
					toBeReturned = "-ERR\r\n";
				}
			} else {
					// som v stave PASSWORD_REQUIRED a neprisiel mi prikaz pass
				threadArgs->sessionState = state_USER_REQUIRED;
				toBeReturned = "-ERR\r\n";
			}
			break;
		case state_TRANSACTION:
		// Na zaciatku transaction state by som mal presunut vsetky spravy z new do cur a zaviest ich do struktury
		// obsahujucej informacie o vsetkych suboroch
		
		default:
			break;
	}
	return toBeReturned;
}

void *service(void *threadid) {
	
	int n, r, connectfd;
	char buf[MAX_BUFFER_SIZE];
	std::string recievedMessage("");
	
	struct argumentsForThreadStructure *my_data = (struct argumentsForThreadStructure *) threadid;

	std::cout << "New thread" << std::endl;

	while ((n = read(my_data->acceptedSockDes, buf, MAX_BUFFER_SIZE)) > 0) {
		for (int i = 0; i < n; ++i) {
			recievedMessage += buf[i];
		}
		std::string response = process_message(recievedMessage, n, my_data);
		std::cout << "My actual state is " << my_data->sessionState << std::endl;
		
		r = write(my_data->acceptedSockDes, response.c_str(), response.length());
		recievedMessage.clear();
		if (r == -1)
			err(1, "write()");
		if (r != response.length())
			errx(1, "write(): Buffer written just partially");
	}
	if (n == -1)
		err(1, "read()");


	printf("close(connectfd)\n");
	close(my_data->acceptedSockDes);
}

int main(int argc, char *argv[])
{
	
	Parameters params;
	params.authFile.clear();
	params.clearPass = false;
	params.port = 0;
	params.maildirPath.clear();
	params.reset = false;

	handleArguments(argc, argv, params.authFile, params.clearPass, params.port, params.maildirPath, params.reset);

	if (params.authFile != "") {
		std::string authFileContent = getFileContent(params.authFile);
		params.username 			= getUsername(authFileContent);
		params.password				= getPassword(authFileContent);
	}

	/*=============================================================*/
	int listenfd;
	
	struct sockaddr_in server;
	struct hostent *hostent;

	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	char hostname[100];
	gethostname(hostname, 100);
	if ((hostent = gethostbyname(hostname)) == NULL) {
		errx(1, "gethostbyname(): %s", hstrerror(h_errno));
	}
	memcpy(&server.sin_addr, hostent->h_addr, hostent->h_length);
	server.sin_port = htons((uint16_t) params.port);

	printf("socket(...)\n");
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		err(1, "socket()");
	}

	int flags = fcntl(listenfd, F_GETFL, 0);
	if (flags < 0) {
		fprintf(stderr,"ERROR: Problem with setting socket in non blocking mode (couldn't get socket flags).\n");
		exit(EXIT_FAILURE);
	}
	fcntl(listenfd, F_SETFL, flags);

	printf("bind(...)\n");
	if (bind(listenfd, (struct sockaddr *)&server, sizeof(server)) == -1) {
		err(1, "bind()");
	}

	printf("listen(..., %d)\n", QUEUE);
	if (listen(listenfd, QUEUE) == -1) {
		err(1, "listen()");
	}

	
	/**------------------------------------------------------------**/
	/**------------------This part is inspired by------------------**/
	/**--------http://developerweb.net/viewtopic.php?id=2933-------**/
	/**------------------------------------------------------------**/
	fd_set readset, tempset;
	int maxfd;
	int peersock, j, result, result1, sent;
	unsigned int len;
	timeval tv;
	char buffer[MAX_BUFFER_SIZE+1];
	sockaddr_in addr;
	std::string lastTimestamp;
	std::vector<ArgsToThread> v;

	/* Here should go the code to create the server socket bind it to a port and call listen
	    listenfd = socket(...);
	    bind(listenfd ...);
	    listen(listenfd ...);
	*/

	FD_ZERO(&readset);
	FD_SET(listenfd, &readset);
	maxfd = listenfd;

	do {
		memcpy(&tempset, &readset, sizeof(tempset));
		tv.tv_sec = 30;
		tv.tv_usec = 0;
		result = select(maxfd + 1, &tempset, NULL, NULL, &tv);

		if (result == 0) {
			printf("select() timed out!\n");
		}
		else if (result < 0 && errno != EINTR) {
			printf("Error in select(): %s\n", strerror(errno));
		}
		else if (result > 0) {

			if (FD_ISSET(listenfd, &tempset)) {
				len = sizeof(addr);
				peersock = accept(listenfd, (struct sockaddr *) &addr, &len);
				if (peersock < 0) {
					printf("Error in accept(): %s\n", strerror(errno));
				}
				else {
					FD_SET(peersock, &readset);
					maxfd = (maxfd < peersock)?peersock:maxfd;
				}

				lastTimestamp = getTimestamp();
				std::string welcomeMsg = "+OK POP3 server ready " + lastTimestamp + "\r\n";
				write(peersock, welcomeMsg.c_str(), welcomeMsg.length());
				
				FD_CLR(listenfd, &tempset);
			}


			for (j=0; j<maxfd+1; j++) {
				

				if (FD_ISSET(j, &tempset)) {

					//Filling structure that I am going to send to thread
					ArgsToThread threadArgs;
					threadArgs.acceptedSockDes = peersock;
					threadArgs.params = &params;
					threadArgs.sessionState = params.clearPass ? state_USER_REQUIRED : state_APOP_REQUIRED;
					threadArgs.timestamp = lastTimestamp;
					v.push_back(threadArgs);

					pthread_t recievingThread;
					int thread_ret_code;
					if ((thread_ret_code = pthread_create(&recievingThread, NULL, service, (void *)&v.at(v.size()-1))) != 0) {
						// TODO - co robit v takomto pripade?
						// Mozno poslat pripojenemu uzivatelovi, nech sa pripoji znova a odpojit ho?
						// Zatial vypisujem hlasku a koncim cely server
						fprintf(stderr,"ERROR: Couldn't create new thread for new connection\n");
						exit(EXIT_FAILURE);
					}
					FD_CLR(j, &readset);

	         }      // end if (FD_ISSET(j, &tempset))
	      }      // end for (j=0;...)
	   }      // end else if (result > 0)
	} while (1);
	/**------------------------------------------------------------**/
	/**------------------End of part inspired by-------------------**/
	/**--------http://developerweb.net/viewtopic.php?id=2933-------**/
	/**------------------------------------------------------------**/



	/*=============================================================*/
	return 0;
}
