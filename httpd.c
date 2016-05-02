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
#include <limits.h>
#include <sys/mman.h>

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

static void * accept_request(void*);
static void error_die(const char *);
static int get_line(int, char *, int);
static int startup(u_short *);
static void sent_count(int client, int count);
static void sent_OK(int client);
static unsigned hash(char *str);

pthread_spinlock_t spin_hash;
pthread_spinlock_t spin_fronta;
std::queue<int> myqueue;
#define MY_CPU_COUNT 7
#define IN_BUF_LEN (16*1024*1024)
#define HASH_TABLE (128*1024*1024)
#define WORD_SPACE (64*1024*1024)
#define HT_TYPE char*

pthread_t tid[MY_CPU_COUNT];          //identifikator vlakna
pthread_attr_t attr[MY_CPU_COUNT];    //atributy vlakna
cpu_set_t cpus[MY_CPU_COUNT];
char buffer_recv[MY_CPU_COUNT][IN_BUF_LEN];
/*word_space*/
/*obsahuje data ve formatu: [(char*)] (pointer na stejny format dat se stejnym*/
/* hashem) [char] (samotne slovo zakoncene '\0')*/
char word_space[WORD_SPACE];
char *word_end;

HT_TYPE hash_table[HASH_TABLE];
int count_words;
int get;

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
#define PACKET 1500
static void * accept_request(void *  param){
	char buf[1024];	
	char data_buf[PACKET];
        char * velky_buffer = (char*) param;
        static char delimit[]=" \n\r\t";
	

while(1){
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
		while ((get_line(client, buf, sizeof(buf))) && buf[0]!='\n'){
			if (buf[8]=='L'){
				length = atoi(&buf[16]);
				break;					
			}	
		}
		while ((get_line(client, buf, sizeof(buf))) && buf[0]!='\n');

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

		#define SLOVA 256 
        	char  *string[SLOVA],*save = NULL;
		char * ulozene_slovo; /*na 0 pozici je (char*), na 8. pozici zacina (char) */
		unsigned hash_arr[SLOVA];		
		int count=SLOVA;
		int all= 0;
		int i = 1;
		string[0] = strtok_r(velky_buffer,delimit,&save);
		hash_arr[0] = hash(string[0]);		
		while (!all){
			for (;i<SLOVA;i++){
				string[i] = strtok_r(NULL,delimit,&save);
				if (string[i]==NULL){
					count=i;
					all=1;
					break;
				}	
				hash_arr[i] = hash(string[i]);
			}
			pthread_spin_lock(&spin_hash);
			/* iteruji po slovech */
			for (i=0;i<count;i++){     
				/* hash nepouzit */
				if (hash_table[hash_arr[i]]==0){
					hash_table[hash_arr[i]]=word_end;
					word_end=stpcpy(word_end+8,string[i])+1;
					count_words++;
				}else{	
					ulozene_slovo = hash_table[hash_arr[i]];
					while(1){

						/* nasel jsem shodu v celem slove */					
						if (!strcmp(string[i],&ulozene_slovo[8])){
							break;
						/* slova se neshoduji */
						}else{
							/* slova se neshoduji a nejsem na konci*/
							if (*(char**)ulozene_slovo){
								ulozene_slovo=*(char**)ulozene_slovo;
								continue;
							/* slova se shoduji - jsem na konci */
							}else{	
								*((char**)ulozene_slovo)=word_end;
								word_end=stpcpy(word_end+8,string[i])+1;								
								count_words++;
								break;
							}	
						}
					}
				}	
        		}
			pthread_spin_unlock(&spin_hash);
			i = 0;
       		}	

		sent_OK(client); /*pokud tohle neodeslu pred zavrenim, klient
				si zahlasi :empty response: */
 		close(client);
		continue;
	}else{ /*GET*/
		get=1;
		for (int i=0;i<MY_CPU_COUNT-1;i++){
			if (pthread_self()!=tid[i]) pthread_join(tid[i], NULL);
		}
		sent_count(client,count_words);		
		get = 0;
		count_words = 0;
		memset(hash_table,0,sizeof(HT_TYPE)*HASH_TABLE);
		memset(word_space,0,WORD_SPACE);
		word_end = word_space;
		/*TODO */

		for (int i=0;i<MY_CPU_COUNT-1;i++){
			pthread_create(&tid[i], &attr[i],accept_request, &buffer_recv[i][0]);
		}						
		
		while ((get_line(client, buf, sizeof(buf))) && buf[0]!='\n');  
 		close(client);
		return NULL;
 	}
}
return NULL;
}
/*****************************************************************************/
static unsigned hash(char *str){
        unsigned hash = 5381;
        int i=0;

	while (str[i]!='\0'){
            hash = ((hash << 5) + hash) + str[i++]; /* hash * 33 + c */
	}
        return hash % HASH_TABLE;
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
static void error_die(const char *sc){
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
	count_words = 0;
	mlockall(MCL_FUTURE | MCL_CURRENT);
	memset(hash_table,0,sizeof(HT_TYPE)*HASH_TABLE);
	memset(word_space,0,WORD_SPACE);
	word_end = word_space;

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
