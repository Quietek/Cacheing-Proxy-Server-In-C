#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LISTENENQ 10
#define MAXLINE  4096
#define MAXBUF   8192
/*
 * error - wrapper for perror
 */
void error(char* msg)
{
    perror(msg);
    exit(0);
}
/*
 * CheckCache - returns an integer indicating if a file isn't in the cache/expired (0),
 *  available in the cache (1), or blacklisted (2)
 */  

int CheckCache(char *host, char *resource, int timeout, char *ip)
{
	//declare variables
	int returnval = 0;
	int blacklisted = 0;
	int found = 0;
	//timer to get system time for timeout
	time_t timer;
	time(&timer);
	//open the cacheinfo file in r+ and the blacklist file in r mode
	FILE *cacheinfo = fopen("cacheinfo.txt","r+");
	FILE *blacklist = fopen("blacklist.txt","r");
	int fd = fileno(cacheinfo);
	//get the full path to a resource including the hostname
	char fullpath[MAXLINE], hashedpath[MAXLINE];
	char *token;
	char c;
	strcpy(fullpath,host);
	strcat(fullpath,"/");
	strcat(fullpath,resource);
	//print the cache lookup resource to the console
	printf("Cached lookup: %s\n",fullpath);
	char line[MAXLINE],linecopy[MAXLINE];
	//check blacklist for matching hostname or ip
	while(fgets(line,MAXLINE,blacklist))
	{
		int i=0;
		c = line[i];
		while(c == host[i] || c == ip[i])
		{
			if(c == host[i] && i == strlen(host)-1)
			{
				returnval = 2;
				blacklisted = 1;
				break;
			}
			else if(c == ip[i] && i == strlen(ip)-1)
			{
				returnval = 2;
				blacklisted = 1;
				break;
			}
			i++;
			c = line[i];
		}
	}
	//close the blacklist
	fclose(blacklist);
	//make sure hostname and ip aren't blacklisted
	if(!blacklisted)
	{
		//read cacheinfo line by line
		flock(fd,LOCK_EX);
		while(fgets(line,MAXLINE,cacheinfo))
		{
			//copy the line into another variable since we are using tokens later
			strcpy(linecopy,line);
			//break line into hashed path and the timer value
			token = strtok(line, " ");
			//compare the token to our hashedpath
			if (!strcmp(fullpath,token))
			{
				found = 1;
				//get the timer value from the second part of the line
				token = strtok(NULL,"");
				//compare the timer value and the last recorded access time to the timeout
				if (timer - atoi(token) > timeout)
				{
					//seek to the beginning of the line
					fseek(cacheinfo,-strlen(linecopy),SEEK_CUR);
					//write the current access time to the file
					fprintf(cacheinfo,"%s %ld\n",fullpath,timer);
					
				}
				//if the timeout is still within range, return a flag indicating you can use the cache
				else
				{
					returnval = 1;
					break;
				}
			}
		}
		//if there was no match, append the path and access time information to the end of the file
		if (!found)
		{
			fseek(cacheinfo,0,SEEK_END);
			fprintf(cacheinfo, "%s %ld\n",fullpath,timer);
		}
		flock(fd,LOCK_UN);
	}
	//close the cacheinfo file and return the corresponding flag
	fclose(cacheinfo);
	return returnval;
}
/*
 * HTTP_err_send - function to send HTTP response if the response is anything other than 200
 */
void *HTTP_err_send(void * vargp, int StatusNum)
{
    //convert our socket to proper formatting
    int listenfd = *((int *)vargp);
    //declare variables
    int len;
    int n;
    char str[MAXLINE];
    bzero(str, MAXLINE);
    //check the HTTP response number and copy the correct response into our string declared earlier
    //these may not be formatted properly, caused problems in the PA2 interview
    if (StatusNum == 400)
    {
        strcpy(str,"HTTP/1.1 400 Bad Request\r\nServer : Web Server in C\r\n\r\n<html><head><title>400 Bad Request</title></head><body><p>400 Bad Request</p></body></html>");
    }
    else if (StatusNum == 403)
    {
		strcpy(str,"HTTP/1.1 403 Forbidden\r\nServer : Web Server in C\r\n\r\n<html><head><title>403 Forbidden</title></head><body><p>403 Forbidden</p></body></html>");
	}
    else if (StatusNum == 404)
    {
        strcpy(str,"HTTP/1.1 404 Not Found\r\nServer : Web Server in C\r\n\r\n<html><head><title>404 Not Found</title></head><body><p>404 Not Found: The requested resource could not be found</p></body></html>");
    }
    else if (StatusNum == 415)
    {
        strcpy(str,"HTTP/1.1 415 Unsupported Media Type\r\nServer : Web Server in C\r\n\r\n<html><head><title>415 Unsupported Media Type</head></title><body><p>415 Unsupported Media Type</p></body></html>");
    }
    
    
    //find the length of the string containing the response
    len = strlen(str);
    //send our HTTP response
    n = send(listenfd, str, len, 0);
    //error handling if response failed to send
    if (n < 0){
        printf("Error sending HTTP response %d\n",StatusNum);
    }
    else{
        printf("Sent HTTP response %d\n", StatusNum);
    }
}
int main(int argc,char* argv[])
{
	/*
	 * variable declarations
	 */ 
    pid_t pid;
    struct sockaddr_in clientaddr,serveraddr;
    struct hostent* host;
    int sockfd,*listenfd,portno,optval,timeout;
    int clientlen = sizeof(struct sockaddr_in);
    
    clock_t start_t;
    start_t = clock();
    
    /*
     * check number of arguments and report proper usage back to the user
     */ 
    if(argc != 3)
    {
		printf ("Usage: %s <port> <timeout>\n", argv[0]);
		exit(1);
	}
	/*
	 * get the port number specified on the command line and convert it to an integer
	 */ 
	portno = atoi(argv[1]);
	/*
	 * get the timeout value from the command line and convert it to an integer
	 */ 
	timeout = atoi(argv[2]);
	/*
	 * clear clientaddr
	 */ 
    bzero((char*)&clientaddr, sizeof(clientaddr));   
    
	/*
	 * declare socket, report error if returns less than 0 
	 */ 
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
    {
		error("ERROR opening socket");
	}
    /* setsockopt: Handy debugging trick that lets 
   	* us rerun the server immediately after we kill it; 
   	* otherwise we have to wait about 20 secs. 
   	* Eliminates "ERROR on binding: Address already in use" error. 
   	*/
	optval = 1;
  	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));
  	/*
   	* build the server's Internet address
   	*/ 
    bzero((char*)&serveraddr,sizeof(serveraddr));
    serveraddr.sin_family=AF_INET;
    serveraddr.sin_port=htons(portno);
    serveraddr.sin_addr.s_addr=INADDR_ANY;
	/* 
   	* bind: associate the parent socket with a port 
   	*/
	bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr));
    
    /*
     * Listen on the port, with a backlog of size LISTENENQ
     */ 
    listen(sockfd,LISTENENQ);
    
        
listen:
    //Accept connection
    listenfd = malloc(sizeof(clientaddr));
    *listenfd=accept(sockfd,(struct sockaddr*)&clientaddr,&clientlen); 
    //fork child process   
    pid=fork();
    //if its a child process
    if (pid!=0)
    {
		//close the listen socket
        close(*listenfd);
        //go back to continue listening on the socket, waiting for a connection
        goto listen;
	}
    else
    {
		//variable declarations
        struct sockaddr_in hostaddr;
        int flag=0;
        int hostflag=0;
        int port=80;
        int n,i,connectsockfd,newsockfd,cacheflag;
        char buf[MAXBUF],cmd[MAXLINE],hostname[MAXLINE],address[MAXLINE],urlext[MAXLINE],protocol[10];
        char* temp=NULL;
        //zero out buffer
        bzero((char*)buf,MAXBUF);
        //receive message from buffer on listenfd
        recv(*listenfd,buf,MAXBUF,0);
        //read command, hostname and protocol version from the buffer
        sscanf(buf,"%s %s %s",cmd,hostname,protocol);
        
        //check buffer formatting to make sure its correctly specifying a GET command, website, and protocol version 
        if(((!strncmp(cmd,"GET",3)))&&(!strncmp(hostname,"http://",7))&&((!strncmp(protocol,"HTTP/1.1",8))||(!strncmp(protocol,"HTTP/1.0",8))))
        {
			//copy the address into a second variable for manipulation later
            strcpy(address,hostname);
            //check for a port number being specified in the GET request
            for(i=7;i<strlen(hostname);i++)
            {
                if(hostname[i]==':')
                {
                    flag=1;
                    break;
                }
            }
            //use strtok to break the hostname into tokens
            temp=strtok(hostname,"//");
            //if a port wasn't specified
            if(!flag)
            {
				//grab the token from the next / in the address
                temp=strtok(NULL,"/");
                //copy the host address into a variable
                strcpy(hostname, temp);
            }
            //if a port was specified
            else
            {
				//grab the hostname from up to the : character
                temp=strtok(NULL,":");
                //copy the hostname into a variable
                strcpy(hostname, temp);
                //get the port from the : to / character
                temp = strtok(NULL, "/");
                //convert the port into an integer
                port = atoi(temp);
            }
            //find the url extension from the address using tokens
			temp=strtok(address,"//");
			temp=strtok(NULL,"/");
			if(temp!=NULL)
			{
				temp=strtok(NULL,"");
				if (temp != NULL)
				{
					strcpy(urlext,temp);
				}
				else
				{
					strcpy(urlext,"");
				}
			}
			//open the ipcache file to check for a cached ip prior to lookup
            FILE *ipcache = fopen("ipcache.txt","r");
            //variable declarations
			char ip[MAXLINE];
			char *token;
			char line[MAXLINE];
			//copy in placeholder value for ip so we can tell if there was a hit in the cache or not
			strcpy(ip," ");
			//read through the ipcache file line by line
			while(fgets(line,MAXLINE,ipcache))
			{
				//use strtok with a space character to find the first section of the line
				token = strtok(line," ");
				//compare the hostname requested to the token we found
				if(!strcmp(hostname,token))
				{
					//if theres a match, grab the second half of the line using strtok
					token = strtok(NULL,"");
					//copy the found ip
					strcpy(ip,token);
					break;
				}
			}
			//close the ipcache
			fclose(ipcache);
			//if the placeholder ip value is seen, we need to lookup using gethostbyname
            if(!strcmp(ip," "))
            {
				//use gethostbyname to find the server address
				host=gethostbyname(hostname);
				//let the console know we had to look up the ip and couldn't use the cache
				printf("Finding Host...\n");
				//look up ip, append it to file, and build host address
				if (host != NULL)
				{
					//flag to indicate the host was found and exists
					hostflag = 1;
					//build the host address
					bzero((char*)&hostaddr,sizeof(hostaddr));
					hostaddr.sin_port=htons(port);
					hostaddr.sin_family=AF_INET;
					bcopy((char*)host->h_addr,(char*)&hostaddr.sin_addr.s_addr,host->h_length);
					//open ipcache to append the hostname mapped to the ip
					FILE *ipcache = fopen("ipcache.txt","a+");
					fprintf(ipcache,"%s %s\n",hostname,inet_ntoa(hostaddr.sin_addr));
					//close the ipcache
					fclose(ipcache);
					//copy the ip we looked up into the ip variable
					strcpy(ip,host->h_addr);
				}
			}
			//if we found a host in the ip cache
			else
			{
				//print info on the host being found to console
				printf("Host found in cache...\n");
				//flag indicating host exists
				hostflag = 1;
				//build address using ip we got from checkcachedips
				bzero((char*)&hostaddr,sizeof(hostaddr));
				hostaddr.sin_port=htons(port);
				hostaddr.sin_family=AF_INET;
				//NOTE: inet_aton is deprecated for ipv6
				//convert ip into format necessary for our built address
				inet_aton(ip,&hostaddr.sin_addr);
			}
			//check the cache for if the requested resource already exists or if its blacklisted or timed out
            cacheflag = CheckCache(hostname,urlext,timeout,ip);
            //indicate what flag we got from our cache check, 0=not found or timed out, 1=usable in cache, 2=blacklisted
            printf("Cache Flag: %d\n",cacheflag);
            //no reference found in cache or cache timed out
            if (cacheflag == 0)
            {
				//indicate to the console whether the host was found
				printf("Host Flag: %d\n",hostflag);
				//if the host exists
				if(hostflag)
				{
					//declare new socket
					newsockfd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
					//connect to the host on the new socket
					connectsockfd=connect(newsockfd,(struct sockaddr*)&hostaddr,sizeof(struct sockaddr));
				
					//print information on the host connection to the console
					printf("\nHostname: %s\nExtension: %s\nPort: %d\nProtocol: %s\nIP: %s\n\n",hostname,urlext,port,protocol,inet_ntoa(hostaddr.sin_addr));
					//zero out the buffer
					bzero((char*)buf,MAXBUF);
				
					//copy our get request into the buffer, with the extension if it's not simply looking for the index
					if(strcmp(urlext,""))
					{
						sprintf(buf,"GET /%s %s\r\nHost: %s\r\nConnection: close\r\n\r\n",urlext,protocol,hostname);
					}
					else
					{
						sprintf(buf,"GET / %s\r\nHost: %s\r\nConnection: close\r\n\r\n",protocol,hostname);
					}
					//build the filepath using the hostname and url extension
					char filepath[MAXLINE];
					int slashcount = 0;
					strcpy(filepath,"www/");
					strcat(filepath,hostname);
					strcat(filepath,"/");
					strcat(filepath,urlext);
					for(int k=0;k<strlen(filepath);k++)
					{
						if(filepath[k] == '/' && slashcount >= 1)
						{
							slashcount += 1;
							filepath[k] = ' ';
						}
						else if (filepath[k] == '/')
						{
							slashcount += 1;
						}
					}
					//print the file path to the console
					printf("Filepath: %s\n",filepath);
					//open the file given the file path with fopen, in order to create the file if it doesn't exist
					FILE *cachefile = fopen(filepath,"w");
					//close the file
					fclose(cachefile);
					//open the file to write to
					int cachefd = open(filepath,O_WRONLY,0);
					int block;
					//send HTTP request on socket
					n=send(newsockfd,buf,strlen(buf),0);
					//error handling for if failure to send message
					if (n < 0)
					{
						printf("Failed to send message on socket\n");
					}
					//if the message sent correctly
					else
					{
						//while there is still data being read from the socket
						while(n>0)
						{
							//zero out the buffer
							bzero((char*)buf,MAXBUF);
							//read the data from the host connection socket
							n=recv(newsockfd,buf,MAXBUF,0);
							//if more data is read from the socket
							if(!(n<=0))
							{
								//send data to the client
								send(*listenfd,buf,n,0);
								//printf("Writing to file...\n");
								write(cachefd,buf,n);
							}
						}
					}
					close(cachefd);
					//zero out the buffer
					bzero(buf,MAXBUF);
				}
				//if the host lookup failed
				else
				{
					HTTP_err_send(listenfd, 404);
				}
			}
			//reference available in cache
			else if(cacheflag == 1)
			{
				char filepath[MAXLINE];
				int slashcount = 0;
				strcpy(filepath,"www/");
				strcat(filepath,hostname);
				strcat(filepath,"/");
				strcat(filepath,urlext);
				for(int k=0;k<strlen(filepath);k++)
				{
					if(filepath[k] == '/' && slashcount >= 1)
					{
						slashcount += 1;
						filepath[k] = ' ';
					}
					else if (filepath[k] == '/')
					{
						slashcount += 1;
					}
				}
				int block;
				int cachefd = open(filepath,O_RDONLY, 0);
				while((block = read(cachefd,buf,MAXBUF)) > 0)
				{
					n = send(*listenfd,buf,block,0);
					if (n < 0)
					{
						printf("Error sending on socket...\n");
					}
					bzero(buf,MAXBUF);
				}
				close(cachefd);
			}
			//site blacklisted
			else if(cacheflag == 2)
			{
				//send 403 error
				HTTP_err_send(listenfd,403);
			}
		}
		//if a GET request isn't properly read from the buffer
		else
		{
			//send a 400 error to the client
			HTTP_err_send(listenfd, 400);
		}
		//close sockets
		close(newsockfd);
		close(*listenfd);
		close(sockfd);
		//exit child process
		_exit(0);
    }
    return 0;
}

