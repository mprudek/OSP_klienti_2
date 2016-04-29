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
static void sent_count(int client, int count);
static void sent_OK(int client);

pthread_spinlock_t spin_hash;
pthread_spinlock_t spin_fronta;
std::queue<int> myqueue;
#define MY_CPU_COUNT 5
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
	char data_buf[PACKET];
        int terminate = 0;
        char * velky_buffer = (char*) param;
        static char delimit[]=" \n\r\t";
	

while(!terminate){
	int client;
	pthread_spin_lock(&spin_fronta);
	if (myqueue.empty()){
		if (get){
			pthread_spin_unlock(&spin_fronta);
			pthread_exit(0);
		}else{
			pthread_spin_unlock(&spin_fronta);
		 	continue;
		}
	}else{
		client=myqueue.front();
		myqueue.pop();
		pthread_spin_unlock(&spin_fronta);
	}
		
	int length=0;
	int len=0; 
	int prijato=0;
        z_stream strm;

 	get_line(client, buf, sizeof(buf)); /* POST /osp/myserver/data HTTP/1.1 */

	/*post*/
	if (buf[0]=='P'){
		unsigned char k=0;
		while(1){
			get_line(client, buf2, sizeof(buf2));
			/*newlajna pred prilohou*/
			if (buf2[0]=='\n'){
				break;
			}
			/* delka prilohy je ve 4. lajne hlavicky*/
			if (buf2[8]=='L'){
				unsigned char l=16; /*delka prilohy je na 16 pozici*/
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

		#define SLOVA 500 
        	char *string[SLOVA], *save = NULL;
		int count=SLOVA;
		int all= 0;
		string[0] = strtok_r(velky_buffer,delimit,&save);
		pthread_spin_lock(&spin_hash);
		slova.insert(std::string(string[0]));		
        	pthread_spin_unlock(&spin_hash);
		while (!all){
			for (int i=0;i<SLOVA;i++){
				string[i] = strtok_r(NULL,delimit,&save);
				if (string[i]==NULL){
					count=i;
					all=1;
					break;
				}	
			}
			pthread_spin_lock(&spin_hash);
			for (int i=0;i<count;i++){        		
				slova.insert(std::string(string[i]));		
        		}
			pthread_spin_unlock(&spin_hash);
       		}	

		sent_OK(client); /*pokud tohle neodeslu pred zavrenim, klient
				si zahlasi :empty response: */
 		close(client);
		continue;
	}

	if (buf[0]=='G'){
		get=1;
		for (int i=0;i<MY_CPU_COUNT-1;i++){
			if (pthread_self()!=tid[i]) pthread_join(tid[i], NULL);
		}
		sent_count(client,/* hashset_num_items(set)*/ slova.size());
		slova.clear();
		//pthread_spin_lock(&mutex);			
		get=0;
		/*TODO */

		for (int i=0;i<MY_CPU_COUNT-1;i++){
			pthread_create(&tid[i], &attr[i],accept_request, &buffer_recv[i][0]);
		}			
		//pthread_spin_unlock(&mutex);			
		terminate = 1;
 	}
	
	while ((get_line(client, buf, sizeof(buf))) && buf[0]!='\n');  
	
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
static int get_line(int sock, char *buf, int size){
	int i;

	for (i=0;i<size-1;i++){
  		if (recv(sock, &buf[i], 1, 0)){
   			if (buf[i] == '\r'){
    				recv(sock, &buf[i], 1, 0);
				break;    			
   			}
  		} else break;
 	}
	buf[i+1] = '\0';
 	return i;
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
static int startup(u_short *port){
 	int httpd = 0;
 	struct sockaddr_in name;

 	httpd = socket(AF_INET, SOCK_STREAM, 0);
 	if (httpd == -1){
  		error_die("socket");
	}
 	memset(&name, 0, sizeof(name));
 	name.sin_family = AF_INET;
 	name.sin_port = htons(*port);
 	name.sin_addr.s_addr = htonl(INADDR_ANY);
 	if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0){
  		error_die("bind");
	}
 	if (listen(httpd, MY_CPU_COUNT) < 0){
  		error_die("listen");
	}
 	return(httpd);
}

/**********************************************************************/

int main(void){
 	int server_sock = -1;
 	u_short port = 8080;
 	int client_sock = -1;
 	struct sockaddr_in client_name;
 	int client_name_len = sizeof(client_name);

	get = 0;

	pthread_spin_init(&spin_hash, PTHREAD_PROCESS_PRIVATE);
	pthread_spin_init(&spin_fronta, PTHREAD_PROCESS_PRIVATE);
	
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
		pthread_spin_lock(&spin_fronta);
		myqueue.push(client_sock);
		pthread_spin_unlock(&spin_fronta);
		
 	}
	pthread_spin_destroy(&spin_fronta);
	pthread_spin_destroy(&spin_hash);

 	close(server_sock);
	return(0);
}
