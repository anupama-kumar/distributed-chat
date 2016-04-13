#include "dchat.h"
#include "messagingprotocol.h"
#include "clientmanagement.h"
#include "messagemanagement.h"

void error(char *x){
  perror(x);
  exit(1);
}

// add some way to check if client is alive

bool initialize_data_structures() {
    
  init_list(CLIENTS);
  init_list(UNSEQ_CHAT_MSGS);
  HBACK_Q = init(message_compare,100);

  //  INITIALIZED = TRUE;
  //  return INITIALIZED;
  return TRUE;
}

/*
//TODO a long time from now or probably never
void destroy_data_structures() {

    if (initialized == TRUE) {
        if (clients != NULL) {
            if (clients -> clientlist.clientlist_val != NULL) {
                free(clients->clientlist.clientlist_val);
            }
            free(clients);
        }
        if (msg_buffer != NULL) {
            if (msg_buffer->msg_send !=NULL) {
                free(msg_buffer->msg_send);
            }
            if (msg_buffer->user_sent !=NULL) {
                free(msg_buffer->user_sent);
            }
            //no need to free seq_num and MSG type
        }
    }
    }*/

/*
void shutdown(){
n    
    destroy_data_structures();
    exit(0);
    return NULL;
}

*/

void send_chat_message(int counter, char userinput[])
{
   char* sendstr = strdup(userinput);
   int timestamp = (int)time(NULL);
   char uid[MAXUIDLEN];
   sprintf(uid,"%d^_^%d",timestamp,counter);
   multicast_UDP(CHAT,me->username, uid, sendstr);
   free(sendstr);
}

void *get_user_input(void* t)
{
  char userinput[MAXCHATMESSAGELEN];
  int counter = 0;
  int i = 0;

  if(UIRUNNING)
    initui(0);
  else
  {
    while(1)
    {
      fgets(userinput, sizeof(userinput), stdin);
      if(userinput[0] == '\n')
	continue;
      for(i = 0; i < strlen(userinput); i++)
      {
	if(userinput[i]=='\n')
	  userinput[i] = ' ';
      }
      //    scanf("%s",userinput);
      //    usleep(10000);
      counter++;
      send_chat_message(counter, userinput);
    }
  }
  pthread_exit((void *)t);
}

void *checkup_on_clients(void* t)
{

  int counter = 1;
  while(1)
  {
    sleep(CHECKUP_INTERVAL); // Interval between checkups
    
    int timestamp = (int)time(NULL);
    char uid[MAXUIDLEN];
    sprintf(uid,"%d^_^%d",timestamp,counter);
    
    multicast_UDP(CHECKUP,me->username, uid, "ARE_YOU_ALIVE"); // multicast checkup message to everyone

    pthread_mutex_lock(&CLIENTS->mutex);
    node_t* curr = CLIENTS->head;
    while(curr != NULL)
    {
      // increment everyones counter by one until they respond
      ((client_t*)curr->elem)->missed_checkups++;

      /*
      * Just for debugging purposes to see the current status of missed_checkups for each client
      */      
      printf("%s %s: %d %d\n",((client_t*)curr->elem)->username,
     ((client_t*)curr->elem)->hostname,
     ((client_t*)curr->elem)->portnum,
     ((client_t*)curr->elem)->missed_checkups);


      // check if anyone has missed too many checkups
      if (((client_t*)curr->elem)->missed_checkups > 4)
      {
        printf("I (%s) believe that (%s) is dead :)\n", me->username, ((client_t*)curr->elem)->username);
      }

      curr = curr->next;
    }
    pthread_mutex_unlock(&CLIENTS->mutex);
    counter++;
  }
  pthread_exit((void *)t);
}

void create_message_threads()
{
  int numthreads = 3;
  pthread_t threads[numthreads];
  pthread_attr_t attr;
  void *exitstatus;
  
  //make sure threads are joinable
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  //start a thread for each
  pthread_create(&threads[RECEIVE_THREADNUM], &attr, receive_UDP, (void *)RECEIVE_THREADNUM);
  pthread_create(&threads[SEND_THREADNUM], &attr, get_user_input, (void *)SEND_THREADNUM);
  pthread_create(&threads[CHECKUP_THREADNUM], &attr, checkup_on_clients, (void *)CHECKUP_THREADNUM);

  //pthread_join(threads[RECEIVE_THREADNUM], &exitstatus);
  //  pthread_join(threads[SEND_THREADNUM], &exitstatus);
  //  pthread_join(threads[CHECKUP_THREADNUM], &exitstatus);
}

void join_chat(client_t* jointome, char* myip)
{
  create_message_threads();
  int timestamp = (int)time(NULL);
  char uid[MAXUIDLEN];
  sprintf(uid,"%d^_^%d",timestamp,0);
  char mylocation[MAXPACKETBODYLEN];
  sprintf(mylocation,"%s:%d",myip,LOCALPORT);
  send_UDP(JOIN_REQUEST,"i_not_leader",uid,mylocation,jointome);
}

int main(int argc, char* argv[]){
    
  UNSEQ_CHAT_MSGS = (llist_t*) malloc(sizeof(llist_t));
  CLIENTS = (llist_t*) malloc(sizeof(llist_t));
  initialize_data_structures();


  UIRUNNING = 0;

  char* localport = argv[1];
  char* runui = argv[argc-1];
  printf("I'm awake.\n");

  LOCALPORT = atoi(localport);
  printf("I'm still awake.\n");

  UIRUNNING = atoi(runui);
  printf("I'm extra awake.\n");
  LOCALHOSTNAME = "127.0.0.1";
  if(argc == 5)
  {
    char* remoteip = argv[2];
    char* remoteport = argv[3];
    client_t* jointome = add_client("",remoteip,atoi(remoteport),TRUE);
    //    join_chat(jointome,LOCALHOSTNAME);
  }
  else
  {
    if(LOCALPORT == 6000)
    {
      add_client("i_am_leader","127.0.0.1",5000,TRUE);
      add_client("i_am_follower","127.0.0.1",6000,FALSE);
      //      SEQ_NO = 0;
      //      LEADER_SEQ_NO = 0;
      create_message_threads();
      while(1);
      return 0;
    }

    printf("I'm adding myself.\n");

    add_client("i_am_leader",LOCALHOSTNAME,LOCALPORT,TRUE);
    SEQ_NO = 0;
    LEADER_SEQ_NO = 0;
    printf("I'm starting my threads.\n");
    add_client("i_am_follower","127.0.0.1",6000,FALSE);
    create_message_threads();
    while(1);
    return 0;
  }


  if(strcmp(argv[1],"5000"))
  {
    LOCALPORT = 6000;
  }
  else if(strcmp(argv[1],"6000"))
  {
    LOCALPORT = 5000;    
  }
  LOCALHOSTNAME = "127.0.0.1";
  printf("I'm %d\n",LOCALPORT);
  //add the other fake guy
  add_client("leader\0","127.0.0.1\0",5000,TRUE);
  add_client("follower\0","127.0.0.1\0",6000,FALSE);

  if(me->isleader)
    printf("***I AM LEADER!!!***\n");

  SEQ_NO = 0;
  LEADER_SEQ_NO = 0;
  create_message_threads();

  return 0;
}
