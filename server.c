/* CSCI-4061 Fall 2022
 * Group Member #1: Minrui Tian  tian0138
 * Group Member #2: Wanbo Geng   geng0098
 * Group Member #3: Zhipeng Cao  cao00223
 */

#include "server.h"
#define PERM 0644

//Global Variables [Values Set in main()]
int queue_len           = INVALID;                              //Global integer to indicate the length of the queue
int cache_len           = INVALID;                              //Global integer to indicate the length or # of entries in the cache        
int num_worker          = INVALID;                              //Global integer to indicate the number of worker threads
int num_dispatcher      = INVALID;                              //Global integer to indicate the number of dispatcher threads      
FILE *logfile;                                                  //Global file pointer for writing to log file in worker


/* ************************ Global Hints **********************************/

int req_entries_size = 0;                        //request queue size
int cur_queue_index=0;                           // head of the request queue to dequeue
int cache_size = 0;                             // current size of cache
int cur_cache_index=0;                          // head of the cache queue to be freed


pthread_t worker_thread[MAX_THREADS];           //woker thread array
pthread_t dispatcher_thread[MAX_THREADS];       //dispatcher thread array
int threadID[MAX_THREADS];                      // ID's of threads in a global array


pthread_mutex_t req_queue_lock = PTHREAD_MUTEX_INITIALIZER;        //lock for req_queue r/w
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;              // lock for log_file r/w
pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;            // lock for cache r/w
pthread_cond_t some_content = PTHREAD_COND_INITIALIZER;  // CV indicating waiting for more request to handle
pthread_cond_t free_space = PTHREAD_COND_INITIALIZER;    // CV indicating waiting for space to enqueue

request_t req_entries[MAX_QUEUE_LEN];                    //request queue
cache_entry_t* cache_entries;                          //pointer to cache entries

/**********************************************************************************/


/*
  THE CODE STRUCTURE GIVEN BELOW IS JUST A SUGGESTION. FEEL FREE TO MODIFY AS NEEDED
*/


/* ******************************** Cache Code  ***********************************/

// Function to check whether the given request is present in cache
int getCacheIndex(char *request){
  /* TODO (GET CACHE INDEX)
  *    Description:      return the index if the request is present in the cache otherwise return INVALID
  */
  for (int i = 0; i < cache_len; i++) { // loop through find the corresponding request and return its index
    if(cache_entries[i].request == NULL) { // skip if there is nothing in the cache in case of failture of strcmp
      continue;
    }
    if (strcmp(request,cache_entries[i].request)==0){
      return i;
    }
  }
  return INVALID;       // otherwise return invalid
}

// Function to add the request and its file content into the cache
void addIntoCache(char *mybuf, char *memory , int memory_size){//mybuf is url , memory is the pointer to file content
  /* TODO (ADD CACHE)
  *    Description:      It should add the request at an index according to the cache replacement policy
  *                      Make sure to allocate/free memory when adding or replacing cache entries
  */
  
  cache_entries[(cur_cache_index+cache_size)%cache_len].len= memory_size;                         //dynamically malloc request and content based on how big they are and cpy mybuf and memory
  if((cache_entries[(cur_cache_index+cache_size)%cache_len].request = malloc(strlen(mybuf)+1)) ==NULL){
    printf("cache request malloc fail\n");
  }
  strcpy(cache_entries[(cur_cache_index+cache_size)%cache_len].request,mybuf); 
  if((cache_entries[(cur_cache_index+cache_size)%cache_len].content = malloc(memory_size)) == NULL){
    printf("cache content malloc fail\n");
  }
  memcpy(cache_entries[(cur_cache_index+cache_size)%cache_len].content,memory,memory_size);

  if(cache_size==cache_len){//FIFO
    free(cache_entries[cur_cache_index].request);      //free the oldest 
    free(cache_entries[cur_cache_index].content);
    cur_cache_index = (cur_cache_index+1)%cache_len;      //update the head by 1
    cache_size--;
  }
  cache_size++;
}

// Function to clear the memory allocated to the cache
void deleteCache(){
  /* TODO (CACHE)
  *    Description:      De-allocate/free the cache memory
  */
  for(int i=0;i<cache_len;i++){ // free every request,content and the pointer to cache queue
    free(cache_entries[i].request);
    free(cache_entries[i].content);
  }
  free(cache_entries);


}

// Function to initialize the cache
void initCache(){
  /* TODO (CACHE)
  *    Description:      Allocate and initialize an array of cache entries of length cache size
  */
  if((cache_entries = malloc(sizeof(cache_entry_t)*cache_len))==NULL){
    printf("cache entries malloc fail\n");
  }  //allocate space based on cache_len
  for(int i=0;i<cache_len;i++){  //initialize every member
    cache_entries[i].len= -1;
    cache_entries[i].request=NULL;
    cache_entries[i].content= NULL;
  }
}

/**********************************************************************************/

/* ************************************ Utilities ********************************/
// Function to get the content type from the request
char* getContentType(char *mybuf) {
  /* TODO (Get Content Type)
  *    Description:      Should return the content type based on the file type in the request
  *                      (See Section 5 in Project description for more details)
  *    Hint:             Need to check the end of the string passed in to check for .html, .jpg, .gif, etc.
  */
 
  char* ending = mybuf+strlen(mybuf)-4; //ending of url  mybuf[strlen(mybuf)-4];
  char* ending2 = mybuf+strlen(mybuf)-5;   //ending for .html
  char* content_type;
  if(!strcmp(ending2,".html")){
    content_type = "text/html";
  }
  else if (!strcmp(ending,".htm")){
    content_type =  "text/html";
  }
  else if(!strcmp(ending,".jpg")){
    content_type =  "image/jpeg";
  }
  else if(!strcmp(ending,".gif")){
    content_type =  "image/jif";
  }
  else{
    content_type =  "text/plain";
  }

  return content_type;
}

// Function to open and read the file from the disk into the memory. Add necessary arguments as needed
// Hint: caller must malloc the memory space
int readFromDisk(int fd, char *mybuf, void **memory) { 
  //    Description: Try and open requested file, return INVALID if you cannot meaning error
  if((fd = open(mybuf+1, O_RDONLY)) <0){
    fprintf (stderr, "ERROR: Fail to open the file.\n");
    return INVALID;
  }
  struct stat *buf;
  if((buf = malloc(sizeof(struct stat))) == NULL){
    printf("buffer malloc fail\n");
    return INVALID;
  }
  if(fstat(fd,buf) < 0){
    printf("fail to use fstat\n");
    free(buf);                       //free buf if fstat fail
    return INVALID;
  }
  int filesize = buf->st_size; 
  free(buf);
  if((*memory = malloc(filesize)) == NULL){//malloc memory space for content and free it later 
    printf("memory malloc fail\n");
    return INVALID;
  }
  /* TODO 
  *    Description:      Find the size of the file you need to read, read all of the contents into a memory location and return the file size
  *    Hint:             Using fstat or fseek could be helpful here
  *                      What do we do with files after we open them?
  */
  if(read(fd,*memory,filesize)<0){ // read into memory 
    fprintf (stderr, "ERROR: Fail to read the file.\n");
    return INVALID;
  }
  close(fd);
  return filesize;








  //TODO remove this line and follow directions above
}

/**********************************************************************************/

// Function to receive the path)request from the client and add to the queue
void * dispatch(void *arg) {

  /********************* DO NOT REMOVE SECTION - TOP     *********************/


  /* TODO (B.I)
  *    Description:      Get the id as an input argument from arg, set it to ID
  */
  int threadId = *(int *)arg;
  fprintf(stderr,"dispatchID,%d\n",threadId);
  
  while (1) {

    /* TODO (FOR INTERMEDIATE SUBMISSION)
    *    Description:      Receive a single request and print the conents of that request
    *                      The TODO's below are for the full submission, you do not have to use a 
    *                      buffer to receive a single request 
    *    Hint:             Helpful Functions: int accept_connection(void) | int get_request(int fd, char *filename
    *                      Recommend using the request_t structure from server.h to store the request. (Refer section 15 on the project write up)
    */

  



    /* TODO (B.II)
    *    Description:      Accept client connection
    *    Utility Function: int accept_connection(void) //utils.h => Line 24
    */
    int fd;
    if((fd = accept_connection()) <0){ // error handle
      continue;
    }
  
  



    /* TODO (B.III)
    *    Description:      Get request from the client
    *    Utility Function: int get_request(int fd, char *filename); //utils.h => Line 41
    */
    char *filename;
    if((filename = malloc(1024)) == NULL){
      printf("filename malloc fail\n");
      continue;
    }
    if( get_request(fd,filename) != 0){ //continue?
      continue;
    }
    fprintf(stderr, "Dispatcher Received Request: fd[%d] request[%s]\n", fd, filename);
  


    
    /* TODO (B.IV)
    *    Description:      Add the request into the queue
    */

        //(1) Copy the filename from get_request into allocated memory to put on request queue //do we need to do this?
       
        //(2) Request thread safe access to the request queue
        if(pthread_mutex_lock(&req_queue_lock)<0){
          printf("fail lock req_queue\n");
          continue;
        }
        //(3) Check for a full queue... wait for an empty one which is pthread_cond_pthread_cond_signaled from req_queue_notfull
        while(req_entries_size == queue_len){
          if(pthread_cond_wait(&free_space,&req_queue_lock)!=0){
            printf("fail wait\n");
            continue;
          }
        }
        //(4) Insert the request into the queue
        req_entries[(cur_queue_index + req_entries_size)%queue_len].fd = fd;
        if((req_entries[(cur_queue_index + req_entries_size)%queue_len].request = malloc(strlen(filename)+1))== NULL){
          printf("current queue entires malloc fail\n");
          continue;
        }
        strcpy(req_entries[(cur_queue_index + req_entries_size)%queue_len].request,filename);
        free(filename);
        //(5) Update the queue index in a circular fashion
        req_entries_size++; 
        //(6) Release the lock on the request queue and pthread_cond_pthread_cond_signal that the queue is not empty anymore
        if(pthread_cond_signal(&some_content)!=0){
          printf("fail signal\n");
          continue;
        }
        if(pthread_mutex_unlock(&req_queue_lock)<0){
          printf("fail unlock req_queue\n");
          continue;
        }
 }

  return NULL;
}


/**********************************************************************************/
// Function to retrieve the request from the queue, process it and then return a result to the client
void * worker(void *arg) {
  /********************* DO NOT REMOVE SECTION - BOTTOM      *********************/


  // #pragma GCC diagnostic ignored "-Wunused-variable"      //TODO --> Remove these before submission and fix warnings
  // #pragma GCC diagnostic push                             //TODO --> Remove these before submission and fix warnings


  // Helpful/Suggested Declarations
  int num_request = 0;                                    //Integer for tracking each request for printing into the log
  bool cache_hit  = false;                                //Boolean flag for tracking cache hits or misses if doing 
  int filesize    = 0;                                    //Integer for holding the file size returned from readFromDisk or the cache
  void *memory    = NULL;                                 //memory pointer where contents being requested are read and stored
  char mybuf[BUFF_SIZE];                                  //String to hold the file path from the request

  // #pragma GCC diagnostic pop                              //TODO --> Remove these before submission and fix warnings



  /* TODO (C.I)
  *    Description:      Get the id as an input argument from arg, set it to ID
  */

  int threadId = *(int *)arg;
  fprintf(stderr,"workerID,%d\n",threadId);


  while (1) {
    /* TODO (C.II)
    *    Description:      Get the request from the queue and do as follows
    */
          //(1) Request thread safe access to the request queue by getting the req_queue_mutex lock
          if(pthread_mutex_lock(&req_queue_lock)!=0){
            printf("fail lock req_queue\n");
            continue;
        }
          //(2) While the request queue is empty conditionally wait for the request queue lock once the not empty pthread_cond_pthread_cond_signal is raised
          while(req_entries_size == 0){ //wait for content to work on
            if(pthread_cond_wait(&some_content,&req_queue_lock)!=0){
              printf("fail wait\n");
              continue;
            }
          }
          //(3) Now that you have the lock AND the queue is not empty, read from the request queue
          request_t cur_request;
          cur_request.fd=req_entries[cur_queue_index].fd;
          if((cur_request.request = malloc(strlen(req_entries[cur_queue_index].request)+1)) == NULL){
            printf("current request malloc fail\n");
            continue;
          }
          strcpy(cur_request.request,req_entries[cur_queue_index].request);
          free(req_entries[cur_queue_index].request);
          //(4) Update the request queue remove index in a circular fashion
          cur_queue_index = (cur_queue_index+1)%queue_len;
          req_entries_size--;
          //(5) Check for a path with only a "/" if that is the case add index.html to it 
          if((strcmp(cur_request.request,"/")==0)){
            strcat(strrchr(cur_request.request,'/'),"index.html");
          }
          //(6) Fire the request queue not full pthread_cond_pthread_cond_pthread_cond_signal to indicate the queue has a slot opened up and release the request queue lock
          if(pthread_cond_signal(&free_space)!=0){
            printf("fail signal\n");
            continue;
          }
          if(pthread_mutex_unlock(&req_queue_lock)!=0){
            printf("fail unlock req_queue\n");
            continue;
          }
    /* TODO (C.III)
    *    Description:      Get the data from the disk or the cache 
    *    Local Function:   int readFromDisk(//necessary arguments//);
    *                      int getCacheIndex(char *request);  
    *                      void addIntoCache(char *mybuf, char *memory , int memory_size);  
    */
    strcpy(mybuf,cur_request.request);
    if(pthread_mutex_lock(&cache_lock)<0){
      printf("fail lock cache_lock\n");
    }  // require the lock before r/w cache
    int cache_index;
    if((cache_index =getCacheIndex(mybuf))>=0){ //check if in cache
      cache_hit = true;
      if((memory = malloc(cache_entries[cache_index].len)) == NULL){//malloc for bytes in contents
        printf("malloc bytes in contents fail\n");
      }
      memcpy(memory,cache_entries[cache_index].content,cache_entries[cache_index].len); //copy contents to memory
      filesize=cache_entries[cache_index].len;
      if(pthread_mutex_unlock(&cache_lock)!=0){
          printf("fail unlock cache_lock\n");
        } // release the lock after r/w cache;   // release the lock after r/w cache
    }
    else{
      cache_hit = false;
      if((filesize = readFromDisk(cur_request.fd,mybuf,&memory))<0){
        free(memory);               // free memory if faile
        if(pthread_mutex_unlock(&cache_lock)!=0){
          printf("fail unlock cache_lock\n");
        } // release the lock after r/w cache
      }
      else{
        addIntoCache(mybuf,memory,filesize); //add contents to the cache
        if(pthread_mutex_unlock(&cache_lock)!=0){
          printf("fail unlock cache_lock\n");
        } // release the lock after r/w cache;mutex
      }
    }
    num_request++; //completed request by worker



    /* TODO (C.IV)
    *    Description:      Log the request into the file and terminal
    *    Utility Function: LogPrettyPrint(FILE* to_write, int threadId, int requestNumber, int file_descriptor, char* request_str, int num_bytes_or_error, bool cache_hit);
    *    Hint:             Call LogPrettyPrint with to_write = NULL which will print to the terminal
    *                      You will need to lock and unlock the logfile to write to it in a thread safe manor
    */
    if(pthread_mutex_lock(&log_lock)!=0){
      printf("fail lock log_lock\n");
      continue;
    } //require lock before log r/w
    LogPrettyPrint(logfile,threadId,num_request,cur_request.fd,mybuf,filesize,cache_hit); //write to the file
    LogPrettyPrint(NULL,threadId,num_request,cur_request.fd,mybuf,filesize,cache_hit); //print to the terminal 
    if(pthread_mutex_unlock(&log_lock)!=0){
      printf("fail unlock log_lock\n");
      continue;      
    } //release lock after log r/w


    /* TODO (C.V)
    *    Description:      Get the content type and return the result or error
    *    Utility Function: (1) int return_result(int fd, char *content_type, char *buf, int numbytes); //look in utils.h 
    *                      (2) int return_error(int fd, char *buf); //look in utils.h 
    */
    if(filesize<0){
      if(return_error(cur_request.fd,mybuf)!=0){
          fprintf (stderr, "ERROR: Fail to return the error.\n");
        }
    }
    else{
      if(return_result(cur_request.fd,getContentType(mybuf),memory,filesize)!=0){
        printf("return_result fail\n");
      }
    } 
    free(memory);     // free memory if eveything goes well
    free(cur_request.request); //free the request of cur_request

  }




  return NULL;

}

/**********************************************************************************/

int main(int argc, char **argv) {

  /********************* Dreturn resulfO NOT REMOVE SECTION - TOP     *********************/
  // Error check on number of arguments
  if(argc != 7){
    printf("usage: %s port path num_dispatcher num_workers queue_length cache_size\n", argv[0]);
    return -1;
  }


  int port            = -1;
  char path[PATH_MAX] = "no path set\0";
  num_dispatcher      = -1;                               //global variable
  num_worker          = -1;                               //global variable
  queue_len           = -1;                               //global variable
  cache_len           = -1;                               //global variable


  /********************* DO NOT REMOVE SECTION - BOTTOM  *********************/
  /* TODO (A.I)
  *    Description:      Get the input args --> (1) port (2) path (3) num_dispatcher (4) num_workers  (5) queue_length (6) cache_size
  */
  port = atoi(argv[1]);
  strcpy(path,argv[2]);
  num_dispatcher  = atoi(argv[3]);
  num_worker = atoi(argv[4]);
  queue_len  = atoi(argv[5]);
  cache_len  = atoi(argv[6]);




  /* TODO (A.II)
  *    Description:     Perform error checks on the input arguments
  *    Hints:           (1) port: {Should be >= MIN_PORT and <= MAX_PORT} | (2) path: {Consider checking if path exists (or will be caught later)}
  *                     (3) num_dispatcher: {Should be >= 1 and <= MAX_THREADS} | (4) num_workers: {Should be >= 1 and <= MAX_THREADS}
  *                     (5) queue_length: {Should be >= 1 and <= MAX_QUEUE_LEN} | (6) cache_size: {Should be >= 1 and <= MAX_CE}
  */
 if(port<MIN_PORT || port>MAX_PORT){
   printf("Invalid port: %d\n",port);
   return -1;
 }

 if(opendir(path) == NULL){
   printf("fail to open dir path\n");
 }
 if(!mkdir(path,0777)){ // not exist will return 0 and create one
  printf("Invalid path: %s\n",path);
  return -1;
 }

 if(num_dispatcher<1||num_dispatcher>MAX_THREADS){
   printf("Invalid number for dispatcher: %d\n",num_dispatcher);
   return -1;
 }

 if(num_worker<1||num_worker>MAX_THREADS){
   printf("Invalid number for worker: %d\n",num_worker);
   return -1;
 }
 
 if(queue_len<1||queue_len>MAX_QUEUE_LEN){
   printf("Invalid queue length: %d\n",queue_len);
   return -1;
 }

 if(cache_len<1||cache_len>MAX_QUEUE_LEN){
   printf("Invalid cache length: %d\n",cache_len);
   return -1;
 }



  /********************* DO NOT REMOVE SECTION - TOP    *********************/
  printf("Arguments Verified:\n\
    Port:           [%d]\n\
    Path:           [%s]\n\
    num_dispatcher: [%d]\n\
    num_workers:    [%d]\n\
    queue_length:   [%d]\n\
    cache_size:     [%d]\n\n", port, path, num_dispatcher, num_worker, queue_len, cache_len);
  /********************* DO NOT REMOVE SECTION - BOTTOM  *********************/


  /* TODO (A.III)
  *    Description:      Open log file
  *    Hint:             Use Global "File* logfile", use "web_server_log" as the name, what open flags do you want?
  */

  if((logfile = fopen("web_server_log", "a+")) == NULL){// open the log file
    fprintf (stderr, "ERROR: Fail to open the log file.\n");
    return INVALID;
  }



  /* TODO (A.IV)
  *    Description:      Change the current working directory to server root directory
  *    Hint:             Check for error!
  */
  if (chdir(path)<0){
    printf("Change dir fail");
    return -1;
  }



  /* TODO (A.V)
  *    Description:      Initialize cache  
  *    Local Function:   void    initCache();
  */
  initCache();



  /* TODO (A.VI)
  *    Description:      Start the server
  *    Utility Function: void init(int port); //look in utils.h 
  */
  init(port);



  /* TODO (A.VII)
  *    Description:      Create dispatcher and worker threads 
  *    Hints:            Use pthread_create, you will want to store pthread's globally
  *                      You will want to initialize some kind of global array to pass in thread ID's
  *                      How should you track this p_thread so you can terminate it later? [global]
  */
  for (int i = 0; i < MAX_THREADS; i++) { // thread ID *2????
        threadID[i] = i;
  }
  for(int i = 0; i < num_dispatcher; i++) {
        // Create threads based on num_dispatcher
    if(pthread_create(&(dispatcher_thread[i]), NULL, dispatch, (void *) &threadID[i]) != 0) {
            printf("Dispatch thread %d failed to create\n", i);
    }
       
  }

  for(int i = 0; i < num_worker; i++) {
      // Create threads based on num_worker
      if(pthread_create(&(worker_thread[i]), NULL, worker, (void *) &threadID[i]) != 0) {
           printf("Worker thread %d failed to create\n", i);
      }
  }


  // Wait for each of the threads to complete their work
  // Threads (if created) will not exit (see while loop), but this keeps main from exiting
  int i;
  for(i = 0; i < num_worker; i++){
    fprintf(stderr, "JOINING WORKER %d \n",i);
    if((pthread_join(worker_thread[i], NULL)) != 0){
      printf("ERROR : Fail to join worker thread %d.\n", i);
    }
  }
  for(i = 0; i < num_dispatcher; i++){
    fprintf(stderr, "JOINING DISPATCHER %d \n",i);
    if((pthread_join(dispatcher_thread[i], NULL)) != 0){
      printf("ERROR : Fail to join dispatcher thread %d.\n", i);
    }
  }
  deleteCache(); //delete cache after use
  fprintf(stderr, "SERVER DONE \n");  // will never be reached in SOLUTION
}

