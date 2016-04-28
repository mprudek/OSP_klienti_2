#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <zlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <iostream>       // std::cout
#include <queue>          // std::queue:

#include <unordered_set>
#include <iostream>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

static std::unordered_set<std::string> slova;

static void * accept_request(void*);
static void error_die(const char *);
static int get_line(int, char *, int);
static int startup(u_short *);
static void unimplemented(int);
static void sent_count(int client, int count);
static void sent_OK(int client);

pthread_spinlock_t sem;
pthread_spinlock_t mutex;
std::queue<int> myqueue;
#define MY_CPU_COUNT 6
#define IN_BUF_LEN 16*1024*1024

pthread_t tid[MY_CPU_COUNT];          //identifikator vlakna
pthread_attr_t attr[MY_CPU_COUNT];    //atributy vlakna
cpu_set_t cpus[MY_CPU_COUNT];
char buffer_recv[MY_CPU_COUNT][IN_BUF_LEN];

int get;

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
#define PACKET 1500
static void * accept_request(void *  param){
 	char buf[1024];
	char buf2[1024];
 	char method[255];
 	char url[255];
	char path[512];
	int numchars;
 	size_t i, j;
	unsigned char k,l;
	struct stat st;
	int length=0;	
	char data_buf[PACKET];
	int len;
        int terminate = 0;
	int prijato;
	int client;
        char * velky_buffer = (char*) param;
        static char delimit[]=" \n\r\t";
	
	//printf("vlakno %d bezi\n",(int)param);

while(!terminate){
	pthread_spin_lock(&mutex);
	if (myqueue.empty()){
		if (get){
			pthread_spin_unlock(&mutex);
			pthread_exit(0);
		}else{
			pthread_spin_unlock(&mutex);
		 	continue;
		}
	}else{
		client=myqueue.front();
		myqueue.pop();
		pthread_spin_unlock(&mutex);
	}

		
	numchars=0;
 	i=0;
	j=0;
	k=0;
	l=0;
	length=0;
	len=0; 
	prijato=0;
        z_stream strm;



 	numchars=get_line(client, buf, sizeof(buf)); /* POST /osp/myserver/data HTTP/1.1 */

 	while (!ISspace(buf[j]) && (i < sizeof(method) - 1)){
  		method[i] = buf[j];
  		i++;
		j++;
 	}
 	method[i] = '\0';

 	if (strcasecmp(method, "GET") && strcasecmp(method, "POST")){
  		unimplemented(client);
  		close(client);
		continue;
		
 	}
 	
	/* preskocime mezery */
	while (ISspace(buf[j]) && (j < sizeof(buf))){
		j++;
	}

	/* URL pozadavku - resource */
	i = 0; 	
	while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))){
		url[i] = buf[j];
		i++;
		j++;
	}
	url[i] = '\0';

	if (strcasecmp(method, "POST") == 0){
		/* chybny resource*/
		if (strcmp(url,"/osp/myserver/data")){
 			close(client);
			continue;
		}
		k=0;
		while(1){
			get_line(client, buf2, sizeof(buf2));
		//	printf("%d. lajna hlavicky = %s",k,buf2);
			/*newlajna pred prilohou*/
			if (buf2[0]=='\n'){
				break;
			}
			/* delka prilohy je ve 4. lajne hlavicky*/
			if (buf2[8]=='L'){
				l=16; /*delka prilohy je na 16 pozici*/
				while (buf2[l]!='\n' && buf2[l]!='\r' 
						&& buf2[l]!='\0' ){
					length*=10;
					length+=buf2[l]-48;
					l++;
				}	
			}
			k++;
		}

		inflateInit2(&strm,15 | 32);

		strm.avail_out = IN_BUF_LEN;
		strm.next_out = (unsigned char*)velky_buffer;
			
		while (length){
			//bajty, ktere chci precist
			len = PACKET < length ? PACKET : length; 
			prijato=recv(client,data_buf,len,0);
			length-=prijato;

			strm.avail_in = prijato; //pocet bajtu k dekompresi
			strm.next_in = (unsigned char*)data_buf;	

			inflate(&strm, Z_SYNC_FLUSH); 
		}

		inflateEnd(&strm);
		
        velky_buffer[IN_BUF_LEN-strm.avail_out] = '\0';
        char *string, *save = NULL;

	pthread_spin_lock(&sem);
        string = strtok_r(velky_buffer,delimit,&save);
        while (string != NULL) {		
		slova.insert(std::string(string));		
		string = strtok_r(NULL,delimit,&save);
        }
	pthread_spin_unlock(&sem);
       

		sent_OK(client); /*pokud tohle neodeslu pred zavrenim, klient
				si zahlasi :empty response: */
 		close(client);
		continue;
	}

	if (strcasecmp(method, "GET") == 0){
		if (!strcmp(url,"/osp/myserver/count")){
//			printf("cekam na vlakna\n");
			get=1;
			for (int i=0;i<MY_CPU_COUNT-1;i++){
				if (pthread_self()!=tid[i]) pthread_join(tid[i], NULL);
			}
			sent_count(client,/* hashset_num_items(set)*/ slova.size());
			slova.clear();
			pthread_spin_lock(&mutex);			
			get=0;
			
			/*TODO */
			

			for (int i=0;i<MY_CPU_COUNT-1;i++){
				pthread_create(&tid[i], &attr[i],accept_request, &buffer_recv[i][0]);
			}			
			pthread_spin_unlock(&mutex);			
			terminate = 1;
		}
 	}

	        if (stat(path, &st) == -1) {
                while ((numchars > 0) && strcmp("\n", buf)) { /* read & discard headers */
                        numchars = get_line(client, buf, sizeof(buf));
                }
                //not_found(client);
        }
	
 	close(client);
	continue;
}
	return NULL;
}


/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
static void error_die(const char *sc)
{
 perror(sc);
 exit(1);
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
static int get_line(int sock, char *buf, int size)
{
 int i = 0;
 char c = '\0';
 int n;

 while ((i < size - 1) && (c != '\n'))
 {
  n = recv(sock, &c, 1, 0);
  /* DEBUG printf("%02X\n", c); */
  if (n > 0)
  {
   if (c == '\r')
   {
    n = recv(sock, &c, 1, MSG_PEEK);
    /* DEBUG printf("%02X\n", c); */
    if ((n > 0) && (c == '\n'))
     recv(sock, &c, 1, 0);
    else
     c = '\n';
   }
   buf[i] = c;
   i++;
  }
  else
   c = '\n';
 }
 buf[i] = '\0';
 
 return(i);
}

/**********************************************************************/
/* Testovaci metoda - vraci pouze cislo */
/**********************************************************************/
static void sent_count(int client, int count){
 	char buf[1024];

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, SERVER_STRING);
 	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "\r\n");
 	send(client, buf, strlen(buf), 0);

 	sprintf(buf, "%d",count);
	send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Odpoved klientovi - vse je OK */
/**********************************************************************/
static void sent_OK(int client){
 	char buf[1024];

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, SERVER_STRING);
 	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
 	send(client, buf, strlen(buf), 0);
 	sprintf(buf, "\r\n");
 	send(client, buf, strlen(buf), 0);
}



/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
static int startup(u_short *port)
{
 int httpd = 0;
 struct sockaddr_in name;

 httpd = socket(PF_INET, SOCK_STREAM, 0);
 if (httpd == -1)
  error_die("socket");
 memset(&name, 0, sizeof(name));
 name.sin_family = AF_INET;
 name.sin_port = htons(*port);
 name.sin_addr.s_addr = htonl(INADDR_ANY);
 if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
  error_die("bind");
 if (*port == 0)  /* if dynamically allocating a port */
 {
  int namelen = sizeof(name);
  if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
   error_die("getsockname");
  *port = ntohs(name.sin_port);
 }
 if (listen(httpd, 5) < 0)
  error_die("listen");
 return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
static void unimplemented(int client)
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

int main(void){
 	int server_sock = -1;
 	u_short port = 8080;
 	int client_sock = -1;
 	struct sockaddr_in client_name;
 	int client_name_len = sizeof(client_name);

	get = 0;

	pthread_spin_init(&sem, PTHREAD_PROCESS_PRIVATE);
	pthread_spin_init(&mutex, PTHREAD_PROCESS_PRIVATE);
	
 	server_sock = startup(&port);
 	printf("httpd running on port %d\n", port);

	for (int i=0;i<MY_CPU_COUNT-1;i++){
		pthread_attr_init(&attr[i]);
	}
	for (int i=0;i<MY_CPU_COUNT;i++){
		CPU_ZERO(&cpus[i]);
		CPU_SET(i, &cpus[i]);
	}
	
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpus[MY_CPU_COUNT-1]);
	
	for (int i=0;i<MY_CPU_COUNT-1;i++){
		pthread_attr_setaffinity_np(&attr[i], sizeof(cpu_set_t), &cpus[i]);
	}

	for (int i=0;i<MY_CPU_COUNT-1;i++){
		pthread_create(&tid[i], &attr[i],accept_request, &buffer_recv[i][0]);
	}
	
 	while (1){
  		client_sock = accept(server_sock,
                       (struct sockaddr *)&client_name,
                       &client_name_len);
  		if (client_sock == -1){
   			error_die("accept");
		}
		pthread_spin_lock(&mutex);
		myqueue.push(client_sock);
		pthread_spin_unlock(&mutex);
		
 	}
	pthread_spin_destroy(&mutex);
	pthread_spin_destroy(&sem);

 	close(server_sock);
	return(0);
}
