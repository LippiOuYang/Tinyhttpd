/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h> //socket functions
#include <sys/types.h>  //u_short
#include <netinet/in.h> //sockaddr_in
#include <arpa/inet.h>  //inet_ntoa
#include <unistd.h>
#include <ctype.h> //isspace
#include <strings.h>
#include <string.h>
#include <sys/stat.h> //file status
#include <pthread.h>  //POSIX threads
#include <sys/wait.h> //waitpid
#include <stdlib.h>
#include <stdint.h> //intptr_t

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN 0
#define STDOUT 1
#define STDERR 2

void accept_request(void *);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(void *arg)
{
    int client = (intptr_t)arg; // get client socket
    char buf[1024];             // buffer for request
    size_t numchars;            // number of characters in request
    char method[255];           // request method
    char url[255];              // request url
    char path[512];             // request path
    size_t i, j;                // index for iteration through request
    struct stat st;             // file status
    int cgi = 0;                /* becomes true if server decides this is a CGI
                                 * program */
    char *query_string = NULL;  // query string for GET method

    numchars = get_line(client, buf, sizeof(buf));
    i = 0;
    j = 0;
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1)) // get request method
    {
        method[i] = buf[i]; // store request method
        i++;                // increment index
    }
    j = i;            // store index
    method[i] = '\0'; // null-terminate method

    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) // if method is not GET or POST
    {
        unimplemented(client); // send unimplemented response
        return;
    }

    if (strcasecmp(method, "POST") == 0) // if method is POST
        cgi = 1;                         // set cgi flag

    i = 0;
    while (ISspace(buf[j]) && (j < numchars)) // skip white space
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars)) // get url
    {
        url[i] = buf[j]; // store url
        i++;             // increment index
        j++;             // increment index
    }
    url[i] = '\0'; // null-terminate url

    if (strcasecmp(method, "GET") == 0) // if method is GET
    {
        query_string = url;                                       // get query string
        while ((*query_string != '?') && (*query_string != '\0')) // find query string
            query_string++;                                       // increment index
        if (*query_string == '?')                                 // if query string found
        {
            cgi = 1;              // set cgi flag to true
            *query_string = '\0'; // null-terminate url
            query_string++;       // increment index
        }
    }

    sprintf(path, "htdocs%s", url);    // set path to htdocs directory
    if (path[strlen(path) - 1] == '/') // if path ends with '/'
        strcat(path, "index.html");    // append index.html to path
    if (stat(path, &st) == -1)         // if file does not exist
    {
        while ((numchars > 0) && strcmp("\n", buf))        /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf)); // read headers
        not_found(client);                                 // send not found response
    }
    else
    {
        if ((st.st_mode & S_IFMT) == S_IFDIR) // if path is a directory
            strcat(path, "/index.html");      // append index.html to path
        if ((st.st_mode & S_IXUSR) ||         // if user has execute permission
            (st.st_mode & S_IXGRP) ||         // if group has execute permission
            (st.st_mode & S_IXOTH))           // if others have execute permission
            cgi = 1;                          // set cgi flag to true
        if (!cgi)                             // if not cgi
            serve_file(client, path);

        execute_cgi(client, path, method, query_string);
    }

    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024]; // buffer for reading file

    if (fgets(buf, sizeof(buf), resource) != NULL)
    {                           // read a line from the file
        while (!feof(resource)) // while not end of file
        {
            send(client, buf, strlen(buf), 0);             // send the line to the client
            if (fgets(buf, sizeof(buf), resource) == NULL) // if end of file
                break;
        }
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(EXIT_FAILURE);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path, const char *method, const char *query_string)
{
    char buf[1024];          // buffer for reading request
    int cgi_output[2];       // pipe for output
    int cgi_input[2];        // pipe for input
    pid_t pid;               // process id of child
    int status;              // status of child process
    int i;                   // index for iteration
    char c;                  // character buffer
    int numchars = 1;        // number of characters read
    int content_length = -1; // content length

    buf[0] = 'A';                                          // initialize buffer with 'A'(arbitrary)
    buf[1] = '\0';                                         // null-terminate buffer
    if (strcasecmp(method, "GET") == 0)                    // GET
        while ((numchars > 0) && strcmp("\n", buf))        // read & discard headers
            numchars = get_line(client, buf, sizeof(buf)); // read headers
    else if (strcasecmp(method, "POST") == 0)              // POST
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf)) // read headers
        {
            buf[15] = '\0';                                // null-terminate buffer
            if (strcasecmp(buf, "Content-Length:") == 0)   // if header is Content-Length
                content_length = atoi(&(buf[16]));         // get content length
            numchars = get_line(client, buf, sizeof(buf)); // read headers
        }
        if (content_length == -1) // if content length not found
        {
            bad_request(client);
            return;
        }
    }
    else /*HEAD or other*/
    {
        // do nothing
        return;
    }

    if (pipe(cgi_output) < 0) // check if creating pipe for output failed
    {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) // check if creating pipe for input failed
    {
        cannot_execute(client);
        return;
    }

    if ((pid = fork()) < 0) // check if forking failed
    {
        cannot_execute(client);
        return;
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); // send 200 OK response
    send(client, buf, strlen(buf), 0);
    if (pid == 0) /* child: CGI script */
    {
        char meth_env[255];   // environment variable for request method
        char query_env[255];  // environment variable for query string
        char length_env[255]; // environment variable for content length

        dup2(cgi_output[1], STDOUT);                    // redirect stdout to cgi_output
        dup2(cgi_input[0], STDIN);                      // redirect stdin to cgi_input
        close(cgi_output[0]);                           // close unused read end of output
        close(cgi_input[1]);                            // close unused write end of input
        sprintf(meth_env, "REQUEST_METHOD=%s", method); // set request method
        putenv(meth_env);                               // set environment variable
        if (strcasecmp(method, "GET") == 0)             /*GET */
        {
            sprintf(query_env, "QUERY_STRING=%s", query_string); // set query string
            putenv(query_env);                                   // set environment variable
        }
        else
        {                                                             /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length); // set content length
            putenv(length_env);                                       // set environment variable
        }
        execl(path, path, (char *)NULL); // execute cgi script
        exit(0);
    }
    else
    {                                            /* parent */
        close(cgi_output[1]);                    // close unused write end of output
        close(cgi_input[0]);                     // close unused read end of input
        if (strcasecmp(method, "POST") == 0)     // if method is POST
            for (i = 0; i < content_length; i++) // read content length bytes
            {
                recv(client, &c, 1, 0);
                if (write(cgi_input[1], &c, 1) < 0)
                {
                    perror("write");
                }
            }
        while (read(cgi_output[0], &c, 1) > 0) // read from cgi_output
            send(client, &c, 1, 0);            // send to client

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0); // wait for child process to finish
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;     // index for the buffer (current position)
    char c = '\0'; // character buffer to hold the read character
    int n;         // number of bytes read from the socket

    // continue reading until the buffer is full or a newline character is encountered
    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0); // read one byte from the socket
        /* DEBUG printf("%02X\n", c); */

        if (n > 0) // if a byte was successfully read
        {
            if (c == '\r') // if the character is a carriage return
            {
                n = recv(sock, &c, 1, MSG_PEEK); // peek at the next byte without removing it from the receive queue
                /* DEBUG printf("%02X\n", c); */

                if ((n > 0) && (c == '\n')) // if the next byte is a line feed
                    recv(sock, &c, 1, 0);   // read the line feed, removing it from the receive queue
                else
                    c = '\n'; // if the next byte is not a line feed, set c to line feed
            }
            buf[i] = c; // store the character in the buffer
            i++;        // increment the index
        }
        else
            c = '\n'; // if no byte was read, set c to line feed to end the loop
    }

    buf[i] = '\0'; // null-terminate the buffer

    return (i); // return the number of bytes read (excluding the null terminator)
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename; /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];
    // stadnard response for not found
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL; // file pointer
    int numchars = 1;      // number of characters read
    char buf[1024];        // buffer for reading request

    buf[0] = 'A';                                      // initialize buffer
    buf[1] = '\0';                                     // null-terminate buffer
    while ((numchars > 0) && strcmp("\n", buf))        /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf)); // read headers

    resource = fopen(filename, "r"); // open file
    if (resource == NULL)            // if file not found
        not_found(client);           // send not found response
    else
    {
        headers(client, filename); // send headers
        cat(client, resource);     // send file
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;           // socket descriptor
    int on = 1;              // setsockopt
    struct sockaddr_in name; // socket address

    httpd = socket(PF_INET, SOCK_STREAM, 0); // create socket
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));                                         // clear name avoid garbage
    name.sin_family = AF_INET;                                              // set address family as ipv4
    name.sin_port = htons(*port);                                           // convert port to network byte order
    name.sin_addr.s_addr = htonl(INADDR_ANY);                               // set address to any interface in local machine
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0) // set socket option to reuse address and port(avoid bind error when server restart)
    {
        error_die("setsockopt failed");
    }
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0) // bind socket to address
        error_die("bind");
    if (*port == 0) /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);                                 // length of socket name as argument for getsockname
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1) // get socket name
            error_die("getsockname");
        *port = ntohs(name.sin_port); // set port to actual port
    }

    /*
    In kernel version 2.2 and later,
    the second argument specifies the maximum length of the
    queue of pending connections,
    but this parameter is now deprecated and has no effect.
    So it could be any value greater than 0.
    */
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return (httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;                            // init server_sock as -1
    u_short port = 4000;                             // set listening port to 4000
    int client_sock = -1;                            // init client_sock as -1
    struct sockaddr_in client_name;                  // strut for client name
    socklen_t client_name_len = sizeof(client_name); // length of client name
    pthread_t newthread;                             // thread for new connection

    server_sock = startup(&port); // start server and get socket
    printf("httpd running on port %d\n", port);

    while (1)
    {
        client_sock = accept(server_sock,
                             (struct sockaddr *)&client_name,
                             &client_name_len); // accept connection
        if (client_sock == -1)
            error_die("accept");
        /* accept_request(&client_sock); */
        if (pthread_create(&newthread, NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0) // create worker thread
            perror("pthread_create");
    }

    close(server_sock); // close server socket

    return (0);
}