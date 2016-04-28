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
//#include "hashset.h"

#include <unordered_set>
#include <iostream>

#define TABLE (16777216*2)
//#define TABLE 7

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

//#define DEBUG

struct parametry{
        int client;
//	hashset_t set;
};

static std::unordered_set<std::string> slova;

void * accept_request(void*);
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
void sent_count(int client, int count);
void sent_OK(int client);
int inf(const void *src, int srcLen, void *dst, int dstLen) ;
int parse_words(char * buf, int* first, int buf_size);
//hashset_t set;
//sem_t sem;
pthread_spinlock_t sem;
pthread_spinlock_t mutex;
std::queue<int> myqueue;
pthread_t tid1,tid2,tid3,tid4,tid5, tid_shared;          //identifikator vlakna
//pthread_mutex_t mutex;
pthread_attr_t attr1, attr2, attr3, attr4, attr5;    //atributy vlakna
cpu_set_t cpus1, cpus2, cpus3, cpus4, cpus5;
int get =0;

char buffer_recv[4][32*1024*1024];

static void print_decomp(char * buf, int first, int decomprimed, int buf_size){
	int i;
	if (first+decomprimed<=buf_size){
		for (i=first;i<first+decomprimed;i++){
			printf("index=%d znak=%c\n",i,buf[i]);
		}
	}else{
		for (i=first;i<buf_size;i++){
			printf("index=%d znak=%c\n",i,buf[i]);
		}
		for (i=0;i<first+decomprimed-buf_size;i++){
			printf("index=%d znak=%c\n",i,buf[i]);
		}
	}
}

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
#define PACKET 1500
#define DECOMP_BUF_SIZE 50000
static void * accept_request(void *  param){
 	char buf[1024];
	char buf2[1024];
	char decomp[DECOMP_BUF_SIZE]={'k'};
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
	//z_stream strm  = {0};
	int next_word;
	int out_space; 
	int pred_infl;
        int terminate = 0;
	int dekomprimovano;
	int index_decomp;
	int prijato;
	char zlib_out[DECOMP_BUF_SIZE];
	int client;
	void * retval;
        char * velky_buffer = (char*) param;
        static char delimit[]=" \n\r\t";
	
	//printf("vlakno bezi\n");


while(!terminate){
	pthread_spin_lock(&mutex);
	if (myqueue.empty()){
		if (get){
			pthread_spin_unlock(&mutex);
//			printf("vlakno konci\n");
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
//	printf("vlakno spojeni=%d\n",client);
		
	

	numchars=0;
 	i=0;
	j=0;
	k=0;
	l=0;
	length=0;
	len=0;
	next_word=0;
	out_space=0; 
	pred_infl=0;
	dekomprimovano=0;
	index_decomp=0;
	prijato=0;
        z_stream strm  = {0};



 	numchars=get_line(client, buf, sizeof(buf)); /* POST /osp/myserver/data HTTP/1.1 */
//	printf("head=%s\n",buf);

	i=0;	
	j=0;

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
	//	printf("komprimovano=%d\n",length);

		inflateInit2(&strm,15 | 32);

		//strm.next_in = (unsigned char*)data_buf;
		//strm.avail_out = 5*1024*1024;
		//strm.next_out = (unsigned char*)zlib_out; //adresa prvnoho byte pro dekomp data

		next_word = 0;
		index_decomp = 0;
                int celkem = 0;

		/* DEKOMPRESE - TODO kontrolovat, zda si neprepisuju stara data  */
		/* zatim doufam, ze delka vystupnihu bufferu je dostatecne velkym*/
		/* nasobkem delky vstupniho bufferu 				 */
		while (length){
			//bajty, ktere chci precist
			len = PACKET < length ? PACKET : length; 
			prijato=recv(client,data_buf,len,0);
			length-=prijato;
			//printf("prijato=%d, pozadovano=%d\n",prijato, len);

			strm.avail_in = prijato; //pocet bajtu k dekompresi
			strm.avail_out = 32*1024*1024-celkem;
			strm.next_out = (unsigned char*)velky_buffer+celkem; //&velky_buffer[celkem]
			strm.next_in = (unsigned char*)data_buf;	

			/* updates next_in, avail_in, next_out, avail_out */	
			pred_infl = strm.avail_out;
			#ifdef DEBUG		
			ret = inflate(&strm, Z_SYNC_FLUSH);
			#else 
			inflate(&strm, Z_SYNC_FLUSH);
			#endif  
			dekomprimovano = pred_infl-strm.avail_out;
			
//			for (i=0;i<dekomprimovano;i++){
			//	printf("indx=%d, znk=%c\n",i,zlib_out[i]);
//				decomp[index_decomp++]=zlib_out[i];
//				index_decomp%=DECOMP_BUF_SIZE;	
//			}
                        celkem += dekomprimovano;

			
			//print_decomp(decomp, index_decomp, dekomprimovano,DECOMP_BUF_SIZE );
			//printf("dekopmrimovano=%d celkem=%d\n",dekomprimovano,celkem);

//			
			//printf("outer space po parse=%d\n",out_space);	
		}
//               std::cout << "total out " << strm.total_out << "\n";
//               std::cout << "celkem " << celkem << "\n";
//               std::cout << "length " << length << "\n";

		
		//parse_words(decomp,&next_word,DECOMP_BUF_SIZE);
        velky_buffer[celkem] = '\0';
        char *string, *save = NULL;

        pthread_spin_lock(&sem);
        string = strtok_r(velky_buffer,delimit,&save);
        while (string != NULL) {
            slova.insert(std::string(string));
//            std::cout << "adding |" << string << "|\n";
            string = strtok_r(NULL,delimit,&save);
        }
        pthread_spin_unlock(&sem);
        
	//	printf("koncim vlakno\n");
		inflateEnd(&strm);

		sent_OK(client); /*pokud tohle neodeslu pred zavrenim, klient
				si zahlasi :empty response: */
 		close(client);
		continue;
	}

	if (strcasecmp(method, "GET") == 0){
		if (!strcmp(url,"/osp/myserver/count")){
//			printf("cekam na vlakna\n");
			get=1;
			if (pthread_self()!=tid1) pthread_join(tid1, NULL);
			if (pthread_self()!=tid2) pthread_join(tid2, NULL);
			if (pthread_self()!=tid3) pthread_join(tid3, NULL);
//			if (pthread_self()!=tid5) pthread_join(tid5, NULL);
//			printf("slova=%d\n",hashset_num_items(set));
//                        std::cout << slova.size() << "\n";
			sent_count(client,/* hashset_num_items(set)*/ slova.size());
//			hashset_destroy(set);
			slova.clear();
//			set = hashset_create(TABLE);
			
			pthread_spin_lock(&mutex);			
			get=0;
			int one=0, two=0, three=0, five=0;
//			std::cout << "ziju\n";
//			if (pthread_self()==tid1){
//				pthread_create(&tid_shared, &attr1,accept_request, &buffer_recv[0][0]);
//				one =1;
//			}else if (pthread_self()==tid2){
//				two=1;				
//				pthread_create(&tid_shared, &attr2,accept_request, &buffer_recv[1][0]);
//			}else if (pthread_self()==tid3){
//				three = 1;
//				pthread_create(&tid_shared, &attr3,accept_request, &buffer_recv[2][0]);
//			} /*else if (pthread_self()==tid5){
//				five = 1;
//				pthread_create(&tid_shared, &attr5,accept_request, &buffer_recv[3][0]);
//			}*/
//			std::cout << "ziju\n";
			

			/*if (!one)*/ pthread_create(&tid1, &attr1,accept_request, &buffer_recv[0][0]);

			/*if (!two)*/ pthread_create(&tid2, &attr2,accept_request, &buffer_recv[1][0]);
			/*if (!three)*/ pthread_create(&tid3, &attr3,accept_request, &buffer_recv[2][0]);

			//pthread_create(&tid4, &attr,accept_request, (void *)0);
//			if (!five)pthread_create(&tid5, &attr5,accept_request, &buffer_recv[3][0]);
			pthread_spin_unlock(&mutex);			
//			std::cout << "ziju\n";
			
			terminate = 1;
//			if (set == NULL) {
//				printf("failed to create hashset instance\n");
//			}
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

}



/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
static void cat(int client, FILE *resource)
{
 char buf[1024];

 fgets(buf, sizeof(buf), resource);
 while (!feof(resource))
 {
  send(client, buf, strlen(buf), 0);
  fgets(buf, sizeof(buf), resource);
 }
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

//	sem_init(&sem, 0, 1); //thread-shared, init-value 1
	pthread_spin_init(&sem, PTHREAD_PROCESS_PRIVATE);

	pthread_spin_init(&mutex, PTHREAD_PROCESS_PRIVATE);
	
 	server_sock = startup(&port);
 	printf("httpd running on port %d\n", port);

	pthread_attr_init(&attr1);
	pthread_attr_init(&attr2);
	pthread_attr_init(&attr3);
	pthread_attr_init(&attr4);
	pthread_attr_init(&attr5);

	CPU_ZERO(&cpus1);
	CPU_ZERO(&cpus2);
	CPU_ZERO(&cpus3);
	CPU_ZERO(&cpus4);
	CPU_ZERO(&cpus5);
	CPU_SET(0, &cpus1);
	CPU_SET(1, &cpus2);
	CPU_SET(2, &cpus3);
	CPU_SET(3, &cpus4);
	CPU_SET(4, &cpus5);

	pthread_attr_setaffinity_np(&attr1, sizeof(cpu_set_t), &cpus1);
	pthread_attr_setaffinity_np(&attr2, sizeof(cpu_set_t), &cpus2);
	pthread_attr_setaffinity_np(&attr3, sizeof(cpu_set_t), &cpus3);
	pthread_attr_setaffinity_np(&attr5, sizeof(cpu_set_t), &cpus5);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpus4);

	//set = hashset_create(TABLE);
	printf("1\n");
//	pthread_attr_init(&attr);
	printf("1\n");	
	pthread_create(&tid1, &attr1,accept_request, &buffer_recv[0][0]);
//	pthread_attr_init(&attr);
	pthread_create(&tid2, &attr2,accept_request, &buffer_recv[1][0]);
//	pthread_attr_init(&attr);
	pthread_create(&tid3, &attr3,accept_request, &buffer_recv[2][0]);
//	pthread_attr_init(&attr);
//	pthread_create(&tid5, &attr5,accept_request, &buffer_recv[3][0]);	

/*	if (set == NULL) {
		printf("failed to create hashset instance\n");
             	close(server_sock);
		return 1;
        }
*/	
 	while (1){
  		client_sock = accept(server_sock,
                       (struct sockaddr *)&client_name,
                       &client_name_len);
  		if (client_sock == -1){
   			error_die("accept");
		}
		pthread_spin_lock(&mutex);
//		printf("pridavam spojeni=%d\n",client_sock);
		myqueue.push(client_sock);
		pthread_spin_unlock(&mutex);
//		printf("spojeni pridano=%d\n",client_sock);
		
 	}
	pthread_spin_destroy(&mutex);
	pthread_spin_destroy(&sem);

//	sem_destroy(&sem);
 	close(server_sock);
	return(0);
}
/**********************************************************************/
/* Dekomprimuje obsah bufferu a ulozi ho do dalsihoi bufferu*/
/**********************************************************************/
static int inf(const void *src, int srcLen, void *dst, int dstLen) {
    z_stream strm  = {0};
    strm.total_in  = strm.avail_in  = srcLen;
    strm.total_out = strm.avail_out = dstLen;
    strm.next_in   = (Bytef *) src;
    strm.next_out  = (Bytef *) dst;

    strm.zalloc = Z_NULL;
    strm.zfree  = Z_NULL;
    strm.opaque = Z_NULL;

    int err = -1;
    int ret = -1;

    err = inflateInit2(&strm, (15 + 32)); //15 window bits, and the +32 tells zlib to to detect if using gzip or zlib
    if (err == Z_OK) {
        err = inflate(&strm, Z_FINISH);
        if (err == Z_STREAM_END) {
            ret = strm.total_out;
        }
        else {
             inflateEnd(&strm);
             return err;
        }
    }
    else {
        inflateEnd(&strm);
        return err;
    }

    inflateEnd(&strm);
    return ret;
}
/********************************************************************/
/* Vrati pocet smazanych znaku     */
/********************************************************************/
static int delete_word(char * buf, int first, int last, int buf_size){
	int i;
	int chars = 0;
	if (first<=last){
/*		for (i=first;i<=last;i++){
			buf[i]='\0';
			chars++;
		} */
		memset(&buf[first], '\0', last-first+1);
	}else{
/*		for (i=first;i<buf_size;i++){
			buf[i]='\0';
			chars++;
		}
		for (i=0;i<=last;i++){
			buf[i]='\0';
			chars++;
		} */
		memset(&buf[first], '\0', buf_size-first);
		memset(buf, '\0', last+1);
	}
	return chars;
}
/**********************************************************************/
/* Dekomprimuje obsah bufferu a ulozi ho do dalsihoi bufferu          */
/* Vraci pozici prvniho bajtu prvniho nedokonceneho slova             */
/* Za kazdym slovem ocekavame space character -> isspace(char c)      */
/* Vraci, kolik znaku z fufferu bylo uvolneno                         */
/**********************************************************************/
static int parse_words(char * buf, int* first, int buf_size){
	int main_index=*first; /*iterace pres bajty v bufferu*/
	int word_index=0; /* itrace pres bajty v aktualnim sloce */
	char word[126];
	int chars = 0;
pthread_spin_lock(&sem);
	while(buf[main_index]!='\0'){
		if (isspace(buf[main_index])){
			//printf("pw mezera\n");
			if (word_index){ /*delka slova neni nula*/
				word[word_index]='\0'; /*ukoncim slovo*/
				//sem_wait(&sem);
//				hashset_add(set, (void *)word, word_index,&sem);
//                                pthread_spin_lock(&sem);
				slova.insert((char*) word);
//                                pthread_spin_unlock(&sem);
				//sem_post(&sem);				
				chars+=delete_word(buf, *first, main_index-1, buf_size);
				//printf("pw slovo= %s\n",word);
				word_index=0;
			}
			buf[main_index]='\0';
			/* v pripade, ze koncim mezerou musim dalsi 
			first index nastavit manualne protoze 
			jinak by ukazoval na zacatek posledniho
			- jiz smazaneho slova*/
			*first=(main_index+1)%buf_size; 
			chars++;
		}else{	
			/*prvni pismeno noveho slova*/
			if (!word_index) *first = main_index;
			//printf("pw znak=%c\n",buf[main_index]);
			word[word_index]=buf[main_index];
			word_index++;
		}
		main_index++;
		main_index%=buf_size;
	}
pthread_spin_unlock(&sem);
	return chars;
}
