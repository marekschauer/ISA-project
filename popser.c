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
#include <mutex>          // std::mutex
#include <map>            // std::map
#include <csignal>
#include "md5.h"

using namespace std;

#define BUFFER (2)
#ifndef MAX_BUFFER_SIZE
#define MAX_BUFFER_SIZE (2)
#endif
#define QUEUE  (2)

/**
 * Enum for commands
 */
typedef enum {
   cmd_user,
   cmd_pass,
   cmd_apop,
   cmd_stat,
   cmd_list,
   cmd_retr,
   cmd_dele,
   cmd_noop,
   cmd_rset,
   // cmd_top, 
   cmd_uidl,   
   cmd_quit,
   cmd_unknown
} CommandName;

/**
 * Enum for states of server
 */
typedef enum {
   state_USER_REQUIRED,
   state_WRONG_USERNAME,
   state_PASSWORD_REQUIRED,
   state_APOP_REQUIRED,
   state_TRANSACTION,
   state_UPDATE
} SessionState;

/**
 * Structure that represents one email
 */
typedef struct emails {
   std::string fileName;
   uint size;
   bool toBeDeleted;
   std::string hash;
} EmailsStruct;

/**
 * Structure that contains program parameters
 */
typedef struct programParameters {
   std::string authFile;
   bool clearPass;
   int port;
   std::string maildirPath;
   bool reset;
   std::string username;
   std::string password;
} Parameters;

/**
 * Container for arguments to thread
 */
typedef struct argumentsForThreadStructure {
   int acceptedSockDes;
   SessionState sessionState;
   std::string timestamp;
   Parameters* params;
   std::vector<EmailsStruct> emails;
   pthread_t threadID;
} ArgsToThread;

/**
 * Structure representing one command
 */
typedef struct cmd {
   CommandName name;
   std::string firstArg;
   std::string secondArg;
} Command;

std::vector<ArgsToThread*> threadArgsVec;
std::map<pthread_t,int> threadSockMap;
fd_set readset, tempset;
int listenfd;
std::mutex mtx;           // mutex for critical section

/**
 * @brief      Function for printing help and exiting whole program
 */
void printHelp() {      //TODO
   std::cout << "This will be help, it's gonna be awesome" << endl;
   exit(EXIT_SUCCESS);
}

/**
 * @brief      Checks, whether exists the file of given name.
 *
 * @param[in]  strFileName  The string file name
 *
 * @return     1 when file exists, 0 otherwise
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
 * @brief      Determines if given string is numeric.
 *
 * @param[in]  pszInput     The string to be checked, wheter it contains only digits
 * @param[in]  nNumberBase  The number base
 *
 * @return     True if numeric, False otherwise.
 */
bool isNumeric( const char* pszInput, int nNumberBase )
{
   std::string base = "0123456789ABCDEF";
   std::string input = pszInput;
 
   return (input.find_first_not_of(base.substr(0, nNumberBase)) == std::string::npos);
}


/**
 * @brief      Function for getting arguments, exits when there's something wrong with arguments.
 *             When this function is called and program continues, arguments passed to program are OK.
 *
 * @param[in]  argumentsCount  The arguments count
 * @param      arguments       The arguments
 * @param      rootFolder      The root folder
 * @param      port            The port
 */
void handleArguments(int argumentsCount, char** arguments, std::string &authFile, bool &  clearPass, int &  port, std::string &maildirPath, bool & reset) {
   int ch;
   port = -1;
    authFile = "";            // TODO - ako sa ma defaultne volat autorizacny subor?
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
 * @brief      Splits a filename and returns it
 *
 * @param[in]  str   The string
 * @return     Name of the file
 */
std::string SplitFilename (std::string str) {
   return str.substr(str.find_last_of("/\\")+1);
}

/**
 * @brief      Splits a path and returns it
 *
 * @param[in]  str   The string
 * @return     Name of the file
 */
std::string SplitPath (std::string str) {
   return str.substr(0,str.find_last_of("/\\")+1);
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
int moveFile(std::string file, std::string dest, std::string logFileName) {
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
      std::cout << "Subor " << file << " neexistuje" << std::endl;
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

   if (logFileName != "") {
      std::string toBeWritten("");
      toBeWritten.append(fileName)
                  .append("\n")
                  .append(file)
                  .append("\n")
                  .append(pathToNewFile)
                  .append("\n");
      // std::ifstream infile(logFileName.c_str());
      std::ofstream fout;
      fout.open(logFileName,ios::app);
      fout<<toBeWritten;
      fout.close();
   }

   return 0;
}

/**
 * @brief      This function deletes log for
 *             certain file in log.txt file
 *
 * @param[in]  fileName  The file name, that should be deleted from log
 *
 */
void deleteFromLog(std::string fileName) {
   std::string line("");
   std::string toBeWritten("");
   std::string from("");
   std::string to("");
   std::ifstream fin("log.txt");
   while (std::getline(fin, line)) {
      getline(fin, from);
      getline(fin, to);
      if (line != fileName) {
         toBeWritten.append(line).append("\n")
                     .append(from).append("\n")
                     .append(to).append("\n");
      }
   }
   fin.close();
   std::ofstream fout("log.txt");
   fout<<toBeWritten;
   fout.close();
}

/**
 * @brief      This function is called, when user is authenticated.
 *             It locks mutex and moves all files from Maildir/new to Maildir/cur
 *
 * @param      emails  The emails struct to be filled
 * @param      params  The parameters (because of path to maildir given as program parameters)
 *
 * @return     Returns 0 if user has access to mutex, 1 if mutex is already taken, 2 if new/ or new/ in Maildir does not exist
 */
int userAuthenticated(std::vector<EmailsStruct>& emails, programParameters* params) {
   // std::vector<EmailsStruct> emails;
   std::vector<std::string> fileNames;
   int retc;
   if (mtx.try_lock()) {
      // mutex is free, so I locked it and let's do some stuff
      if (directoryExists(params->maildirPath + "/new") == 0 || directoryExists(params->maildirPath + "/cur") == 0) {
         mtx.unlock();
         return 2;
      }
      readDirectory(params->maildirPath + "/new", fileNames);           // reading new emails


      for (int i = 0; i < fileNames.size(); ++i) {
      // moving the files from new to cur
         if ((retc = moveFile(params->maildirPath + "/new/" + fileNames.at(i), params->maildirPath + "/cur/", "log.txt")) != 0) {
         // TODO - presun suboru nevysiel, co teraz?
            ;
         }
      }

      fileNames.clear();
      readDirectory(params->maildirPath + "/cur", fileNames);
      for (int i = 0; i < fileNames.size(); ++i) {
         EmailsStruct tmp;
         tmp.fileName = fileNames.at(i);
         std::string fileNameWithPath(params->maildirPath + "/cur/");
         fileNameWithPath.append(fileNames.at(i));
         tmp.size = filesize(fileNameWithPath.c_str());
         tmp.toBeDeleted = false;
         tmp.hash = md5(fileNames.at(i));
         emails.push_back(tmp);
         std::cout << "Email " << emails.at(i).fileName << " has size " << emails.at(i).size << "B" << std::endl;
         emails.at(i);
      }   
      return 0;
   } else {
      // mutex is locked
      return 1;
   }
   
}

/**
 * @brief      Checks for space after command
 *
 * @param[in]  message  The message
 *
 * @return     True if there is a space (ASCII value 32) on 5th position, false otherwise
 */
bool checkForSpaceAfterCommand(std::string message) {
   return (message.substr(4,1) == " ") ? true : false;
}

/**
 * @brief      Gets the command from string, case insensitive
 *
 * @param[in]  message  The message
 *
 * @return     The command.
 */
Command getCommand(std::string message) {
   Command toBeReturned;
   toBeReturned.name = cmd_unknown;
   toBeReturned.firstArg.clear();
   toBeReturned.secondArg.clear();

   if (message.empty() || message.length() < 4) {
      return toBeReturned;
   }

   std::string usersCommand = uppercase(message.substr(0,4)); // TODO - osetri, ak pride prikaz s dlzkou menej ako 4
                                                              // TODO - moze byt aj prikaz TOP, ten ma len 3 znaky   

   if (usersCommand == "USER" && checkForSpaceAfterCommand(message)) {
      toBeReturned.name       = cmd_user;
      toBeReturned.firstArg   = message.substr(5, message.length()-7);
   } else if (usersCommand == "PASS" && checkForSpaceAfterCommand(message)) {
      toBeReturned.name       = cmd_pass;
      toBeReturned.firstArg   = message.substr(5, message.length()-7);
   } else if (usersCommand == "APOP" && checkForSpaceAfterCommand(message)) {
      std::string arguments   = message.substr(5, message.length()-7);
      toBeReturned.name       = cmd_apop;
      toBeReturned.firstArg   = arguments.substr(0, arguments.find(" "));
      toBeReturned.secondArg  = arguments.substr(arguments.find(" ")+1, arguments.length()-arguments.find(" "));
   } else if (usersCommand == "STAT") {
      toBeReturned.name    = cmd_stat;
   } else if (usersCommand == "RETR") {
      if (checkForSpaceAfterCommand(message) && isNumeric(message.substr(5, message.length()-7).c_str(), 10)) {
         toBeReturned.name    = cmd_retr;
         toBeReturned.firstArg = message.substr(5, message.length()-7);
      }
   } else if (usersCommand == "UIDL") {
      toBeReturned.name       = cmd_uidl;
      if (checkForSpaceAfterCommand(message)) {
         toBeReturned.firstArg = message.substr(5, message.length()-7);
         if (!isNumeric(toBeReturned.firstArg.c_str(), 10)) {
            toBeReturned.firstArg.clear();
            // std::string arguments = message.substr(5, message.length()-7); // TODO - why is this line here?
         }
      }
   } else if (usersCommand == "LIST") {
      toBeReturned.name       = cmd_list;
      if (checkForSpaceAfterCommand(message)) {
         toBeReturned.firstArg = message.substr(5, message.length()-7);
         if (!isNumeric(toBeReturned.firstArg.c_str(), 10)) {
            toBeReturned.firstArg.clear();
            std::string arguments = message.substr(5, message.length()-7); // TODO - why is this line here?
         }
      }
   } else if (usersCommand == "DELE") {
      if (checkForSpaceAfterCommand(message) && isNumeric(message.substr(5, message.length()-7).c_str(), 10)) {
         toBeReturned.name    = cmd_dele;
         toBeReturned.firstArg = message.substr(5, message.length()-7);
      }
   } else if (usersCommand == "NOOP") {
      toBeReturned.name = cmd_noop;
   } else if (usersCommand == "QUIT") {
      toBeReturned.name       = cmd_quit;
   }

   return toBeReturned;
}

void printEmails(std::vector<EmailsStruct> emails) {
   for (int i = 0; i < emails.size(); ++i) {
      std::cout << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
      std::cout << emails.at(i).fileName << std::endl;
      std::cout << emails.at(i).size << std::endl;
      std::cout << emails.at(i).toBeDeleted << std::endl;
      std::cout << emails.at(i).hash << std::endl;
   }
}

/**
 * @brief      This function adds termination octet before termination octet
 *             in "\n.\r\n" or "\n.\n" substring of argument toBeHandled
 *
 * @param[in]  toBeHandled  String to be searched for "\n.\r\n" or "\n.\n" substring
 *
 * @return     Returns the converted string, from "\n.\r\n" is "\n..\r\n" and from "\n.\n" happens "\n..\n"
 */
std::string replaceSingleDot(std::string toBeHandled) {
   char actualChar;
   char prevPrevChar = toBeHandled[0];
   char prevChar = toBeHandled[1];
   for (unsigned int i = 2; i < toBeHandled.length(); ++i) {
      actualChar = toBeHandled[i];
      if (prevPrevChar == '\n' && prevChar == '.' && (actualChar == '\n' || actualChar == '\r') ) {
         toBeHandled.insert(i-1, ".");
      }
      prevPrevChar = prevChar;
      prevChar = actualChar;
   }
   return toBeHandled;
}

/**
 * @brief      This function processes the message
 * 
 *
 * @param[in]  message     The message from client
 * @param      threadArgs  The thread arguments
 *
 * @return     { description_of_the_return_value }
 */
std::string process_message(std::string message, argumentsForThreadStructure* threadArgs) {
   
   Command actualCommand = getCommand(message);
   std::cout << "----------------------------------------------" << std::endl;
   std::cout << "Prisiel mi command no. " << actualCommand.name << std::endl;
   std::cout << "Prvy argument: ------->" << actualCommand.firstArg << std::endl;
   std::cout << "Druhy argument: ------>" << actualCommand.secondArg << std::endl;
   std::cout << "Je zapnuta podpora prenosu hesla v nesifrovanej podobe? " << threadArgs->params->clearPass << std::endl;
   std::string toBeReturned("");
   toBeReturned.clear();

   switch(threadArgs->sessionState) {
      case state_USER_REQUIRED:
         // TODO - moze mi sem prist aj prikaz QUIT, dorob
         if (actualCommand.name == cmd_user && threadArgs->params->clearPass) {
            toBeReturned = "+OK\r\n";
            if (threadArgs->params->username == actualCommand.firstArg) {
               threadArgs->sessionState = state_PASSWORD_REQUIRED;
            } else {
               threadArgs->sessionState = state_WRONG_USERNAME;
            }
         } else if (actualCommand.name == cmd_quit) {
            // Here will be QUIT handling
            toBeReturned.append("+OK\r\n");
         } else if (actualCommand.name != cmd_unknown) {
            toBeReturned = "-ERR, unexpected command, expecting USER\r\n";
         } else {
               // pripad, ze som v stave USER_REQUIRED a neprisiel mi prikaz user
            toBeReturned = "-ERR, unknown command\r\n";
         }
         break;
      case state_WRONG_USERNAME:
         // next state will be always state_USER_REQUIRED
         threadArgs->sessionState = state_USER_REQUIRED;
         if (actualCommand.name == cmd_pass) {
            toBeReturned = "-ERR sorry, no mailbox for these credetials\r\n";
         } else if (actualCommand.name == cmd_quit) {
            toBeReturned.append("+OK\r\n");
         } else if (actualCommand.name != cmd_unknown) {
            toBeReturned = "-ERR, unexpected command, expected PASS, now expecting USER\r\n";
         } else {
            // som v stave PASSWORD_REQUIRED a neprisiel mi prikaz pass
            toBeReturned = "-ERR, unknown command, expected PASS, now expecting USER\r\n";
         }
         break;
      case state_PASSWORD_REQUIRED:
         // TODO - moze mi sem prist aj prikaz QUIT, dorob
         if (actualCommand.name == cmd_pass) {
            if (threadArgs->params->password == actualCommand.firstArg) {
               int retUA = userAuthenticated(threadArgs->emails, threadArgs->params);
               if (retUA == 0) {
                  // User presiel cez mutex a vzal si ho
                  threadArgs->sessionState = state_TRANSACTION;
                  toBeReturned = "+OK\r\n";
               } else {
                  // Mutex already taken
                  threadArgs->sessionState = state_USER_REQUIRED;
                  if (retUA == 1) {
                     toBeReturned = "-ERR, mutex is locked, try to log in again\r\n";
                  } else if (retUA == 2) {
                     toBeReturned = "-ERR, problem with Maildir, try to log in again\r\n";
                  }
               }
            } else {
               threadArgs->sessionState = state_USER_REQUIRED;
               toBeReturned = "-ERR sorry, no mailbox for these credetials\r\n";
            }
         } else if (actualCommand.name == cmd_quit) {
            toBeReturned.append("+OK\r\n");
         } else if (actualCommand.name != cmd_unknown) {
            threadArgs->sessionState = state_USER_REQUIRED;
            toBeReturned = "-ERR, unexpected command, expected PASS, now expecting USER\r\n";
         } else {
            // som v stave PASSWORD_REQUIRED a neprisiel mi prikaz pass
            threadArgs->sessionState = state_USER_REQUIRED;
            toBeReturned = "-ERR, unknown command\r\n";
         }
         break;
      case state_APOP_REQUIRED:
         if (actualCommand.name == cmd_apop) {
            if (threadArgs->params->username == actualCommand.firstArg) {
               // Now user is ok, let's test hash equality
               std::string myHash = md5(threadArgs->timestamp + threadArgs->params->password);
               if (myHash == actualCommand.secondArg) {
                  // cez mutex, dorob
                  int retUA = userAuthenticated(threadArgs->emails, threadArgs->params);
                  if (retUA == 0) {
                  // User presiel cez mutex a vzal si ho
                     threadArgs->sessionState = state_TRANSACTION;
                     toBeReturned = "+OK\r\n";
                  } else {
                  // Mutex already taken
                     threadArgs->sessionState = state_APOP_REQUIRED;
                     if (retUA == 1) {
                        toBeReturned = "-ERR, mutex is locked, try to log in again\r\n";
                     } else if (retUA == 2) {
                        toBeReturned = "-ERR, problem with Maildir, try to log in again\r\n";
                     }
                  }
               } else {
                  toBeReturned = "-ERR\r\n";
               }
            } else {
               toBeReturned = "-ERR\r\n";
            }
         } else if (actualCommand.name == cmd_quit) {
            toBeReturned.append("+OK\r\n");
         } else {
            // som v stave state_APOP_REQUIRED a neprisiel mi prikaz pass
            toBeReturned = "-ERR Unknown command, expecting apop\r\n";
         }
         break;
      case state_TRANSACTION:
         // TODO - moze mi sem prist aj prikaz QUIT, dorob
         if (actualCommand.name == cmd_stat) {
            int numberOfEmails   = 0;
            int sizeOfEmails     = 0;
            printEmails(threadArgs->emails);
            for (int i = 0; i < threadArgs->emails.size(); ++i) {
               if (!threadArgs->emails.at(i).toBeDeleted) {
                  numberOfEmails++;
                  sizeOfEmails += threadArgs->emails.at(i).size;
               }
            }
            toBeReturned.append("+OK ")
                     .append(to_string(numberOfEmails))
                     .append(" ")
                     .append(to_string(sizeOfEmails))
                     .append("\r\n");
         } else if (actualCommand.name == cmd_list) {
            // TODO - vyradit z vypisu tie emaily, ktore su oznacene na vymazanie
            //       - asi spravene, over este
            // TODO - osetrovat pristupy do vektora, aby som nedaval index mimo hranice
            if (actualCommand.firstArg == "") {
               int numberOfEmails   = 0;
               int sizeOfEmails  = 0;
               for (int i = 0; i < threadArgs->emails.size(); ++i) {
                  // loop for counting of size
                  if (!threadArgs->emails.at(i).toBeDeleted) {
                     // only if the email is not marked as deleted
                     numberOfEmails++;
                     sizeOfEmails += threadArgs->emails.at(i).size;
                  }
               }
               toBeReturned.append("+OK ")
                        .append(to_string(numberOfEmails))
                        .append(" messages (")
                        .append(to_string(sizeOfEmails))
                        .append(" octets)")
                        .append("\r\n");

               // Vypis jednotlivych emailov pod seba
               for (int i = 0; i < threadArgs->emails.size(); ++i) {
                  if (!threadArgs->emails.at(i).toBeDeleted) {
                     toBeReturned.append(to_string(i+1))
                              .append(" ")
                              .append(std::to_string(threadArgs->emails.at(i).size))
                              .append("\r\n");
                  }
               }
               toBeReturned.append(".\r\n");
            } else {
               int i_dec = std::stoi (actualCommand.firstArg, NULL);
               if (i_dec > static_cast<int>(threadArgs->emails.size())) {
                  toBeReturned.append("-ERR no such message, only ")
                              .append(std::to_string(static_cast<int>(threadArgs->emails.size())))
                              .append(" messages in maildrop\r\n");
               } else if (i_dec < 1) {
                  toBeReturned.append("-ERR message ID must be greater than zero\r\n");
               } else {
                  if (!threadArgs->emails.at(i_dec-1).toBeDeleted) {
                     toBeReturned.append("+OK ")
                                 .append(actualCommand.firstArg)
                                 .append(" ")
                                 .append(std::to_string(threadArgs->emails.at(i_dec-1).size))
                                 .append("\r\n");
                  } else {
                     toBeReturned.append("ERR- message not found\r\n");
                  }
               }
            }
         } else if (actualCommand.name == cmd_uidl) {
            if (actualCommand.firstArg == "") {
               toBeReturned.append("+OK\r\n");
               for (int i = 0; i < threadArgs->emails.size(); ++i) {
                  if (!threadArgs->emails.at(i).toBeDeleted) {
                     toBeReturned.append(to_string(i+1))
                                 .append(" ")
                                 .append(threadArgs->emails.at(i).hash)
                                 .append("\r\n");
                  }
               }
               toBeReturned.append(".\r\n");
            } else {
               int i_dec = std::stoi (actualCommand.firstArg, NULL);
               if (i_dec > static_cast<int>(threadArgs->emails.size())) {
                  toBeReturned.append("-ERR no such message, only ")
                              .append(std::to_string(static_cast<int>(threadArgs->emails.size())))
                              .append(" messages in maildrop\r\n");
               } else if (i_dec < 1) {
                  toBeReturned.append("-ERR message ID must be greater than zero\r\n");
               } else {
                  if (!threadArgs->emails.at(i_dec-1).toBeDeleted) {
                     toBeReturned.append("+OK ")
                                 .append(actualCommand.firstArg)
                                 .append(" ")
                                 .append(threadArgs->emails.at(i_dec-1).hash)
                                 .append("\r\n");
                  } else {
                     toBeReturned.append("ERR- message not found\r\n");
                  }
               }
            }
         } else if (actualCommand.name == cmd_dele) {
            int mailID;
            mailID = std::stoi (actualCommand.firstArg, NULL);

            if (mailID < 1 || mailID > threadArgs->emails.size()) {  // 0 < message id <= vectorsize
               toBeReturned.append("-ERR no such message\r\n");
            } else {
               if (!threadArgs->emails.at(mailID-1).toBeDeleted) {
                  threadArgs->emails.at(mailID-1).toBeDeleted = true;
                  toBeReturned.append("+OK message ")
                           .append(actualCommand.firstArg)
                           .append(" deleted\r\n");
               } else {
                  toBeReturned.append("-ERR message ")
                           .append(actualCommand.firstArg)
                           .append(" already deleted\r\n");
               }
            }
         } else if (actualCommand.name == cmd_retr) {
            std::string filePath = threadArgs->params->maildirPath;
            int mailID;
            mailID = std::stoi (actualCommand.firstArg, NULL);

            if (mailID < 1 || mailID > threadArgs->emails.size()) {  // 0 < message id <= vectorsize
               toBeReturned.append("-ERR no such message\r\n");
            } else {
               if (!threadArgs->emails.at(mailID-1).toBeDeleted) {
                  std::cout << "fileName..." << threadArgs->emails.at(mailID-1).fileName << std::endl;
                  std::cout << "UIDL..." << threadArgs->emails.at(mailID-1).hash << std::endl;
                  filePath.append("/cur/").append(threadArgs->emails.at(mailID-1).fileName);
                  toBeReturned.append("+OK\r\n");
                  toBeReturned.append(getFileContent(filePath));
                  toBeReturned = replaceSingleDot(toBeReturned);
                  toBeReturned.append("\r\n.\r\n");
               } else {
                  toBeReturned.append("-ERR message ")
                              .append(actualCommand.firstArg)
                              .append(" is deleted\r\n");
               }
            }
         } else if (actualCommand.name == cmd_quit) {
            threadArgs->sessionState = state_UPDATE;
            toBeReturned.append(process_message("", threadArgs));
         } else if (actualCommand.name == cmd_noop) {
            toBeReturned.append("+OK\r\n");
         } else if (actualCommand.name == cmd_unknown) {
            toBeReturned.append("-ERR, unknown command\r\n");
         } else {
            toBeReturned.append("-ERR, invalid command\r\n");
         }
         break;
      case state_UPDATE:
         std::cout << "Mam pristup k emailom?" << threadArgs->emails.size() << std::endl;
         bool notRemovedFlag;
         notRemovedFlag = false;
         for (int i = 0; i < threadArgs->emails.size(); ++i) {
            if (threadArgs->emails.at(i).toBeDeleted) {
               std::string filePath = threadArgs->params->maildirPath;
               filePath.append("/cur/").append(threadArgs->emails.at(i).fileName);
               if (fileExists(filePath.c_str())) {
                  if( remove(filePath.c_str()) != 0 ) {
                     // TODO - co teraz?
                     notRemovedFlag = true;
                  } else {
                     deleteFromLog(SplitFilename(filePath));
                  }
               }
            }
         }
         if (notRemovedFlag) {
            toBeReturned.append("-ERR some deleted messages not removed\r\n");
         } else {
            toBeReturned.append("+OK\r\n");
         }
         mtx.unlock();
         break;
      default:
         break;
   }
   return toBeReturned;
}

/**
 * @brief      This function frees the memory allocated for pointer to arguments
 *             for specific thread and than erases this pointer from threadArgs vector.
 *
 * @param[in]  timestamp  The timestamp sent to user to identify the thread
 */
void deleteFromArgsVector(std::string timestamp) {
   for (int i = 0; i < threadArgsVec.size(); ++i) {
      if (timestamp == threadArgsVec.at(i)->timestamp) {
         delete threadArgsVec.at(i);
         threadArgsVec.erase(threadArgsVec.begin()+i);
      }
   }
}

void *service(void *threadid) {
   int n, r, result, connectfd;
   char buf[MAX_BUFFER_SIZE];
   std::string recievedMessage("");

   struct argumentsForThreadStructure *my_data = (struct argumentsForThreadStructure *) threadid;
   std::cout << my_data->timestamp << std::endl;
   std::cout << my_data << std::endl;
   my_data->threadID = pthread_self();
   std::cout << "***************************\n" << my_data->threadID << "\n***************************\n" << std::endl;
   int socDescriptor = my_data->acceptedSockDes;

   // Initialisation of parameters for select function
   fd_set descriptorSet, threadTempset;
   timeval tmvl;
   int selectResult;


   // Sending welcome message
   std::string welcomeMsg = "+OK POP3 server ready "; 
   welcomeMsg.append(my_data->timestamp);
   welcomeMsg.append("\r\n");
   write(socDescriptor, welcomeMsg.c_str(), welcomeMsg.length());
      
   FD_ZERO(&descriptorSet);
   FD_SET(socDescriptor, &descriptorSet);

   while(1) {
      memcpy(&threadTempset, &descriptorSet, sizeof(threadTempset));
      tmvl.tv_sec = 600;
      tmvl.tv_usec = 0;
      selectResult = select(socDescriptor+1, &threadTempset, NULL, NULL, &tmvl);

      if (selectResult == 0) {
         // Timed out
         deleteFromArgsVector(my_data->timestamp);
         FD_CLR(socDescriptor, &readset);
         close(socDescriptor);
         threadSockMap.erase(my_data->threadID);
         mtx.unlock();
         pthread_exit(NULL);
      } else if (selectResult < 0) {
         std::cout << "Some problem with select() (" << strerror(errno) << ")" << std::endl;
      }

      if (FD_ISSET(socDescriptor,&threadTempset)) {
         

         do {
            result = recv(socDescriptor, buf, MAX_BUFFER_SIZE, 0);
            for (int i = 0; i < result; ++i) {
               recievedMessage += buf[i];
            }
            memset(buf, 0, MAX_BUFFER_SIZE);
            // TODO - asi by sa zisli nejake osetrovacky, ked
            // recv vrati nieco mensie nez 0 a podobne
         } while (result > 0);



         if (result == 0) {
            // Client is disconnected
            // TODO - asi by som mal vsetky emaily oznacene na zmazanie odznacit
            //          (If a session terminates for some reason other than a client-issued
            //          QUIT command, the POP3 session does NOT enter the UPDATE state and
            //          MUST not remove any messages from the maildrop.)
            deleteFromArgsVector(my_data->timestamp);
            FD_CLR(socDescriptor, &readset);
            close(socDescriptor);
            threadSockMap.erase(my_data->threadID);
            mtx.unlock();
            pthread_exit(NULL);
         }
      }

      std::string response = process_message(recievedMessage, my_data);
      std::cout << "How long is the message?..." << response.size() << std::endl;
      std::cout << "Write returned ... " << write(socDescriptor, response.c_str(), response.size()) << std::endl;
      if (getCommand(recievedMessage).name == cmd_quit && response == "+OK\r\n") {
         deleteFromArgsVector(my_data->timestamp);
         FD_CLR(socDescriptor, &readset);
         close(socDescriptor);
         threadSockMap.erase(my_data->threadID);
         mtx.unlock();
         pthread_exit(NULL);
      }
      recievedMessage.clear();
   }

   // printf("clxxose(connectfd)\n");
   // close(my_data->acceptedSockDes);
}


/**
 * @brief      Closes all connections, cancels all threads,
 *             unlocks mutex and deallocates all arguments
 *             to threads stored in threadArgsVec
 *             
 *
 * @param[in]  signum  The signum
 */
void signalHandler(int signum) {
   // TODO - mal by som overovat, ci je to SIGINT?
	int ret;
	std::map<pthread_t,int>::iterator it;
	for (it=threadSockMap.begin(); it!=threadSockMap.end(); ++it) {
		std::cout << "Idem zavriet vlakno s descriptorom " << it->second << std::endl;
		ret = close(it->second);
		std::cout << "Podarilo sa? " << ret << std::endl;
		pthread_cancel(it->first);
	}
	std::cout << "Ako dopadol close na listenfd? " << close(listenfd) << std::endl;
	mtx.unlock();
	for (int i = 0; i < threadArgsVec.size(); ++i) {
		delete threadArgsVec.at(i);
	}
	threadArgsVec.clear();
	exit(0);
}

int main(int argc, char *argv[])
{
   signal(SIGINT, signalHandler);
   
   std::cout << "V threadArgsVec je " << threadArgsVec.size() << " prvkov" << std::endl;


   Parameters params;
   params.authFile.clear();
   params.clearPass = false;
   params.port = 0;
   params.maildirPath.clear();
   params.reset = false;

   handleArguments(argc, argv, params.authFile, params.clearPass, params.port, params.maildirPath, params.reset);

   if (params.reset) {
      std::ifstream fin("log.txt");
      std::string line("");
      while (std::getline(fin, line)) {
         std::string from;
         std::string to;
         getline(fin, to);
         getline(fin, from);
         to = SplitPath(to);
         int ret = moveFile(from, to, "");
         remove(from.c_str());
      }
      remove("log.txt");
      return 0;
   }


   if (params.authFile != "") {
      std::string authFileContent = getFileContent(params.authFile);
      params.username         = getUsername(authFileContent);
      params.password            = getPassword(authFileContent);
   }

   /*=============================================================*/
   
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
   int enabled = 1;
   if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(int)) < 0) {
      err(1, "setsockopt()");
   }

   int flags = fcntl(listenfd, F_GETFL, 0);
   if (flags < 0) {
      fprintf(stderr,"ERROR: Problem with setting socket in non blocking mode (couldn't get socket flags).\n");
      exit(EXIT_FAILURE);
   }
   flags |= O_NONBLOCK;
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
   int maxfd;
   int peersock, j, result, result1, sent;
   unsigned int len;
   timeval tv;
   char buffer[MAX_BUFFER_SIZE+1];
   sockaddr_in addr;
   std::string lastTimestamp;

   FD_ZERO(&readset);
   FD_SET(listenfd, &readset);
   maxfd = listenfd;

   do {
      memcpy(&tempset, &readset, sizeof(tempset));
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      result = select(maxfd + 1, &tempset, NULL, NULL, &tv);

      if (result == 0) {
         // printf("select() timed out!\n");
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
               FD_CLR(listenfd, &tempset);
            }

            // Mal by som nastavit aj priznaky pre peersock pomocou fcntl
            int flags = fcntl(peersock, F_GETFL, 0);
            if (flags < 0) {
               fprintf(stderr,"ERROR: Problem with setting socket in non blocking mode (couldn't get socket flags).\n");
               exit(EXIT_FAILURE);
            }
            flags |= O_NONBLOCK;
            fcntl(peersock, F_SETFL, flags);

            /*-----------------------*/
            lastTimestamp = getTimestamp();
            ArgsToThread* threadArgs = new ArgsToThread;
            threadArgs->acceptedSockDes = peersock;
            threadArgs->params = &params;
            threadArgs->sessionState = params.clearPass ? state_USER_REQUIRED : state_APOP_REQUIRED;
            threadArgs->timestamp = lastTimestamp;
            threadArgsVec.push_back(threadArgs);

            // TODO - toto by som asi mal mazat pri ukoncovani threadu
            // mal by som to supnut aj do threadArgs, pretoze ak sa ukonci dany thread
            // (napr. cez QUIT), tak potrebujem zrusit prislusnu dvojicu v threadSockMap
            // inak nebudem vediet, ktora dvojica patri mojmu vlaknu a nebudem vediet, co vymazat
            // alebo mozno aj nie? Uvidime
            pthread_t recievingThread;
            std::cout << "~~~~~" << recievingThread << "~~~~~" << std::endl;
            int thread_ret_code;
            if ((thread_ret_code = pthread_create(&recievingThread, NULL, service, (void *)threadArgsVec.at(threadArgsVec.size()-1))) != 0) {
                  // TODO - co robit v takomto pripade?
                  // Mozno poslat pripojenemu uzivatelovi, nech sa pripoji znova a odpojit ho?
                  // Zatial vypisujem hlasku a koncim cely server
               fprintf(stderr,"ERROR: Couldn't create new thread for new connection\n");
               exit(EXIT_FAILURE);
            }
            threadSockMap[recievingThread] = threadArgs->acceptedSockDes;
            std::map<pthread_t,int>::iterator it;
            for (it=threadSockMap.begin(); it!=threadSockMap.end(); ++it) {
              std::cout << "mehehe" << it->first << " => " << it->second << '\n';
            }

            /*-----------------------*/
         }
      }      // end else if (result > 0)
   } while (1);
   /**------------------------------------------------------------**/
   /**------------------End of part inspired by-------------------**/
   /**--------http://developerweb.net/viewtopic.php?id=2933-------**/
   /**------------------------------------------------------------**/

   /*=============================================================*/
   return 0;
}
