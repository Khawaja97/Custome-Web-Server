/* Copyright Khawaja Shahzaib Ahmad [2020]
 * A simple web-server.  
 * 
 * The web-server performs the following tasks:
 * 
 *     1. Accepts connection from a client.
 *     2. Processes cgi-bin GET request.
 *     3. If not cgi-bin, it responds with the specific file or a 404.
 * 
 */

#include <ext/stdio_filebuf.h>
#include <unistd.h>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>

// Using namespaces to streamline code below
using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;

/** Forward declaration for method defined further below.  You will
    need to use this url_decode method in your serveClient method.
 */
std::string url_decode(std::string data);

// Named-constants to keep pipe code readable below
const int READ = 0, WRITE = 1;

/**
 * This method outputs the top message from the program
 * excluding the first line. This message is common to both cases i.e
 * when a program is to be run or when there is an error 
 * 
 * @param os a reference to the output stream to send data to client 
 */
void printRegular(std::ostream& os) {
    string content = "Content-Type: text/plain\r\n";
    const string transEnc = "Transfer-Encoding: chunked\r\n";
    const string conn = "Connection: Close\r\n";
    os << content;
    os << transEnc;
    os << conn;
    os << "\r\n";
}

/**
 * This method replaces running program with a different program.
 * But process does not change.
 * 
 * @param argList takes in a string vector containing a list of arguments.
 * @return on success exec calls never return 
 */
void myExec(vector<string> argList) {
    vector<char*> args;
    for (auto& s : argList) {
        args.push_back(&s[0]);  // address of first character
    }
    args.push_back(nullptr);
    execvp(args[0], &args[0]);    
}

/**
 * This method handles the error case. It prints the appropriate error message
 * along with it's size in hex
 * 
 * @param line a constant reference to the first line of the input data
 * so that we can extract the file name 
 * @param os The output stream to send data to client 
 */
void errorCase(const string& line, std::ostream& os) {
        // find the position of the "GET " 
        int pos1 = line.find("GET ");
        // find the string "HTTP.." 
        int pos2 = line.find("HTTP/1.1"); 
        // the file name will be in between 
        int diff = pos2 -pos1;
        // extract the name of the file e.g blah.txt
        string fileName = line.substr(pos1+5, diff-5);
        // this is the message about to be printed 
        string message = "Invalid request: " + fileName;
        // get the size of the message about to be printed 
        os << std::hex << message.size() << "\r\n";
        // print the error message 
        os << "Invalid request: "<< fileName << endl;
        os << "\r\n";
        os << "0\r\n\r\n";
}

/**
 * This helper method prints the actual data and it's size in 
 * hex using an output stream. It's for the case where a 
 * program is to be run.
 * 
 * @param is The input stream to read data from client 
 * @param os The Output stream to send data to client 
 */
void outputSizeAndData(std::istream& is, std::ostream& os) {
    string line;
        while (std::getline(is, line)) { 
        // Process child data
            line += "\n";
            // printing chunk size in hex 
            os << std::hex << line.size() << "\r\n";
            // printing the output from the child process 
            os << line << "\r\n";
        }  
}

/**
 * This method calls on myExec to run the specified command and obtains
 * its output using pipes. 
 * 
 * @param myVec a string vector containing decoded cmd and args
 * @param os The output stream to send data to client 
 */
void OutputChildProcess(vector<string> myVec, std::ostream& os) {
    // execute the commands and output 
     int pipefd[2];
    pipe(pipefd);
    const pid_t pid = fork();
    // child process 
    if (pid == 0) {
        close(pipefd[READ]);
        dup2(pipefd[WRITE], WRITE);
        myExec(myVec);
    // parent process 
    } else {     
        close(pipefd[WRITE]);
        __gnu_cxx::stdio_filebuf<char> fb(pipefd[READ], std::ios::in, 1);
        std::istream is(&fb);
        // using helper method to print size in hex and the actual data
        outputSizeAndData(is, os);
        // an int variable to store exit code 
        int exitCode = 0;   
        waitpid(pid, &exitCode, 0);
        //  converting the hex exit code into decimal
        // the message about to be printed
        string message = "Exit code: " + std::to_string(exitCode);
        // the size of the message about to be printed
        os << std::hex << message.size() << "\r\n";
        os << "Exit code: " <<  std::dec << exitCode << "\r\n"; 
        // end of data 
        os << "0\r\n\r\n";         
    }
}

/**
 * This helper method extracts cmd and args from the input,
 * decodes them and stores them is a string vector. It's only 
 * called when it's not an error case.
 * 
 * @param line a constant reference to the first line of the request 
 * @return a string vector containing decoded command and arguments
 */
vector<string> extractData(const string& line) {
    vector<string> result; 
    // the command is always between first "=" and first "&" 
        int pos1 = line.find("=");
        int pos2 = line.find("&");
        int cmdLength = pos2 - pos1;
        // get the command and save it in a string 
        string cmd = line.substr(pos1 + 1, cmdLength -1);
        // use the decode method to decode it
        string decCmd = url_decode(cmd);
        // store the decoded cmd in the vector 
        result.push_back(decCmd);
        // the args are between "args=" and "HTTP" 
        int pos3 = line.find("args=");
        int pos4 = line.find("HTTP");
        int argmLength = pos4 - pos3;
        // extract the arguments
        string argm = line.substr(pos3 + 5, argmLength - 5);
        // decode the arguments 
        string decArgm = url_decode(argm);
        // convert args string into string stream to split on " "  
        std::istringstream ss(decArgm);
        // a string to hold each argument 
        string each;
        while (ss >> std::quoted(each)) {
            result.push_back(each);
        }
        return result;
}

/**
 * This method determines which case it is i.e a program to be run
 * or an error case and calls on the appropriate helper methods to deal
 * with each case
 * 
 * @param line a constant reference to the first line of the request 
 * @param os The output stream to send data to the client 
 */
void stringProcessing(const string& line, std::ostream& os) {
    // a vector to store decoded command and args
    vector<string> result;
    // get the first 17 characters 
    string str = line.substr(0, 17);
    // if it's the case where a program is to be run 
    if (str == "GET /cgi-bin/exec") {
       // if it's a program to be run 
       os << "HTTP/1.1 200 OK\r\n"; 
       // print the rest of the regular message at top     
       printRegular(os); 
       // use helper method to extract cmd & args, decode them
       // and store them in a string vector 
       result = extractData(line);
       // call the helper method to execute decoded cmd & args  
       OutputChildProcess(result, os);  
    } else {
       // print the error message at the top 
       os << "HTTP/1.1 404 Not Found\r\n";
       // print the rest of the message at top     
       printRegular(os);
       // call the method to handle the error case 
       errorCase(line, os);  
    }
}

/**
 * Process HTTP request (from first line & headers) and
 * provide suitable HTTP response back to the client.
 * 
 * @param is The input stream to read data from client.
 * @param os The output stream to send data to client.
 */
void serveClient(std::istream& is, std::ostream& os) {
    // Implement this method as per homework requirement. Obviously
    // you should be thinking of structuring your program using helper
    // methods first. You should add comemnts to your helper methods
    // and then go about implementing them.    
    string firstLine;
    // get the first line 
    getline(is, firstLine); 
    // use helper method below to see which case to run 
    stringProcessing(firstLine, os);
    string rest;
    // ignoring the rest, I guess 
    while (getline(is, rest) && rest != "\r" && rest != "") {}       
}

// -----------------------------------------------------------
//       DO  NOT  ADD  OR MODIFY CODE BELOW THIS LINE
// -----------------------------------------------------------

/** Convenience method to decode HTML/URL encoded strings.

    This method must be used to decode query string parameters
    supplied along with GET request.  This method converts URL encoded
    entities in the from %nn (where 'n' is a hexadecimal digit) to
    corresponding ASCII characters.

    \param[in] str The string to be decoded.  If the string does not
    have any URL encoded characters then this original string is
    returned.  So it is always safe to call this method!

    \return The decoded string.
*/
std::string url_decode(std::string str) {
    // Decode entities in the from "%xx"
    size_t pos = 0;
    while ((pos = str.find_first_of("%+", pos)) != std::string::npos) {
        switch (str.at(pos)) {
            case '+': str.replace(pos, 1, " ");
            break;
            case '%': {
                std::string hex = str.substr(pos + 1, 2);
                char ascii = std::stoi(hex, nullptr, 16);
                str.replace(pos, 3, 1, ascii);
            }
        }
        pos++;
    }
    return str;
}

/**
 * Runs the program as a server that listens to incoming connections.
 * 
 * @param port The port number on which the server should listen.
 */
void runServer(int port) {
    io_service service;
    // Create end point
    tcp::endpoint myEndpoint(tcp::v4(), port);
    // Create a socket that accepts connections
    tcp::acceptor server(service, myEndpoint);
    std::cout << "Server is listening on port "
              << server.local_endpoint().port() << std::endl;
    // Process client connections one-by-one...forever
    while (true) {
        tcp::iostream client;
        // Wait for a client to connect
        server.accept(*client.rdbuf());
        // Process information from client.
        serveClient(client, client);
    }
}

/*
 * The main method that performs the basic task of accepting connections
 * from the user.
 */
int main(int argc, char** argv) {
    if (argc == 2) {
        // Process 1 request from specified file for functional testing
        std::ifstream input(argv[1]);
        serveClient(input, std::cout);
    } else {
        // Run the server on some available port number.
        runServer(0);
    }
    return 0;
}

// End of source code
