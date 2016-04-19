#include "messagingprotocol.h"
#include "clientmanagement.h"

packet_t* parsePacket(char* buf){
  packet_t* input = malloc(sizeof(packet_t));
  strcpy(input->sender,strtok(buf,PACKETDELIM));
  strcpy(input->senderuid,strtok(NULL,PACKETDELIM));
  strcpy(input->uid,strtok(NULL,PACKETDELIM));
  input->packettype = atoi(strtok(NULL,PACKETDELIM));
  input->packetnum = atoi(strtok(NULL,PACKETDELIM));
  input->totalpackets = atoi(strtok(NULL,PACKETDELIM));
  strcpy(input->packetbody,strtok(NULL,PACKETDELIM));
  return input;
}

void free_packet(packet_t* packet)
{
  //  free(packet->sender);
  //free(packet->uid);
  ///free(packet->packetbody);
  free(packet);
  return;
}


chatmessage_t* process_packet(chatmessage_t* message, packet_t* newpacket)
{
  //if so, and if the message is not complete, add this packet's contents to it
  //if not, create a new chatmessage
  if(message)
    append_to_chatmessage(message, newpacket);
  else
  {
    message = create_chatmessage(newpacket);
    add_elem(UNSEQ_CHAT_MSGS, (void*)message);
  }
  return message;
}

void assign_sequence(chatmessage_t* message)
{
  char seqnum[5];
  pthread_mutex_lock(&seqno_mutex);
  sprintf(seqnum,"%d",LEADER_SEQ_NO);
  multicast_UDP(SEQUENCE,me->username,me->uid,message->uid,seqnum);
  LEADER_SEQ_NO++;
  pthread_mutex_unlock(&seqno_mutex);
}

void exit_chat(chatmessage_t* message){
    
    char buffer[10];
    fgets(buffer,10,stdin);
    
    if ((int) strtol(buffer,NULL,10) == 0) {
        // now that ctrl+d has been pressed , do this
        
      multicast_UDP(EXIT,me->username,me->uid,message->uid, NULL);
        
    }
}


void sequence(chatmessage_t* message, packet_t* newpacket)
{
  message->seqnum = atoi(newpacket->packetbody);
  remove_elem(UNSEQ_CHAT_MSGS,(void*)message);
  q_enqueue(HBACK_Q,(void*)message);
  chatmessage_t* firstmessage = (chatmessage_t*)q_peek(HBACK_Q);
  pthread_mutex_lock(&seqno_mutex);
  if(firstmessage->messagetype == JOIN && SEQ_NO == -1) //my first message to display!
  {
    SEQ_NO = firstmessage->seqnum;
  }
  if(firstmessage->seqnum <= SEQ_NO)
  {
    SEQ_NO = firstmessage->seqnum + 1;
    client_t* firstclientmatchbyname;
    if(firstmessage->messagetype == CHAT)
    {
      printf("\E[34m%s\E(B\E[m (sequenced: %d):\t%s\n", firstmessage->sender, firstmessage->seqnum,firstmessage->messagebody);
      firstclientmatchbyname = find_first_client_by_username(firstmessage->sender);
    }
    else
    {
      printf("\E[34m%s\E(B\E[m joined the chat (sequenced: %d)\n", firstmessage->messagebody, firstmessage->seqnum);
      firstclientmatchbyname = find_first_client_by_username(firstmessage->messagebody);
    }

    char* hostname = "";
    int portnum = -1;
	    
    if(firstclientmatchbyname != NULL)
    {
      hostname = firstclientmatchbyname->hostname;
      portnum = firstclientmatchbyname->portnum;
    }
    if(firstmessage->messagetype == CHAT)
      print_msg_with_senderids(firstmessage->sender,firstmessage->messagebody, hostname, portnum);
    else if(firstmessage->messagetype == JOIN)
      print_info_with_senderids(firstmessage->messagebody,"has joined the chat",hostname,portnum);
    q_dequeue(HBACK_Q);
  }
  pthread_mutex_unlock(&seqno_mutex);

  return;
}

void process_late_sequence(chatmessage_t* message, packet_t* newpacket)
{
  pthread_mutex_lock(&STRAY_SEQ_MSGS->mutex);
  node_t* curr = STRAY_SEQ_MSGS->head;
  packet_t* seqpacket = NULL;
  bool founddeqpacket = FALSE;
  while(curr != NULL)
  {
    seqpacket = (packet_t*)curr->elem;
    if(seqpacket->uid == newpacket->uid)
    {
      founddeqpacket = TRUE;
      break;
    }
    curr = curr->next;
  }
  pthread_mutex_unlock(&STRAY_SEQ_MSGS->mutex);
  if(founddeqpacket)
  {
    remove_elem(STRAY_SEQ_MSGS,seqpacket);
    sequence(message, seqpacket);
  }
  return;
}

void join_chat(client_t* jointome)
{
  char uid[MAXUIDLEN];
  get_new_uid(uid);
  char mylocation[MAXPACKETBODYLEN];
  sprintf(mylocation,"%s:%d",LOCALHOSTNAME,LOCALPORT);
  printf("Sending JOIN_REQUEST %s to %s:%d\n", mylocation, jointome->hostname, jointome->portnum);
  send_UDP(JOIN_REQUEST,LOCALUSERNAME,mylocation,uid,mylocation,jointome);
  free(jointome);
}

void *receive_UDP(void* t)
{
    
    struct sockaddr_in addr;
    int fd;
    int nbytes;
    socklen_t addrlen;
    char buf[MAXPACKETLEN];

    //fd = socket(PF_INET,SOCK_DGRAM,0);
    fd = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }
    
    //setup destination address
    
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY); /* N.B.: differs from sender */
    addr.sin_port=htons(LOCALPORT);
    //    printf("Binding %d\n",LOCALPORT);
    
    //bind to receive address
    
    if (bind(fd,(struct sockaddr *) &addr,sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }
    pthread_mutex_unlock(&messaging_mutex);//unlocks lock from create_message_threads
    
    while (1) {
        addrlen = sizeof(addr);
        
        nbytes = recvfrom(fd,buf,MAXPACKETLEN,0,(struct sockaddr *) &addr,&addrlen);
        
	//printf("RECEIVED: %s\n",buf);

        if (nbytes <0) {
            perror("recvfrom");
            exit(1);
        }

	packet_t* newpacket = parsePacket(buf);
	chatmessage_t* message;
	char newusername[MAXSENDERLEN];
	char newip[MAXPACKETBODYLEN];
	int newport;
	node_t* curr = STRAY_SEQ_MSGS->head;
	//figure out what type of packet this is and act accordingly
	switch(newpacket->packettype)
	{
	case CHAT:

	  //figure out if this corresponds to an existing chatmessage
	  message = find_chatmessage(newpacket->uid);
	  message = process_packet(message,newpacket);

	  if(message->iscomplete && me->isleader)
	    assign_sequence(message);

	  //for now, just print if it's complete
	  if(message->iscomplete)
	    printf("\E[33m%s\E(B\E[m (not sequenced):\t%s\n", message->sender, message->messagebody);

	  //check the stray sequencing list. If there's already something in there that matches, remove it and add the message to the priority queue.
	  process_late_sequence(message, newpacket);

	  free_packet(newpacket);
	  break;
	case SEQUENCE:
	  //This is a sequencing message. Find the corresponding chat message in the unsequenced message list and enqueue it properly
	  message = find_chatmessage(newpacket->uid);
	  //If the corresponding message is not complete, ask the leader for its missing part first. It will be received as a chat. TODO
	  //if no message is found, put this sequencing packet in the stray sequencing list
	  if(message == NULL)
	  {
	    add_elem(STRAY_SEQ_MSGS,newpacket);
	    break;
	  }

	  sequence(message, newpacket);


	  //check if the front of the queue corresponds to our expected current sequence no. If so, print it. If not, we should wait or eventually ping the leader for it.

	  free_packet(newpacket);
	  break;
	case CHECKUP:
	  if (strcmp(newpacket->packetbody,"ARE_YOU_ALIVE") == 0)
	  {
	    // send udp message to sender saying that this client is still alive
	    // printf("I (%s) am gonna send alive response to (%s)\n", me->username, newpacket->sender);
	    
	    client_t* orig_sender = find_first_client_by_username(newpacket->sender);
	    send_UDP(CHECKUP, me->username, me->uid,newpacket->uid, "I_AM_ALIVE", orig_sender);
	  }
	  else if (strcmp(newpacket->packetbody, "I_AM_ALIVE") == 0)
	  {
	    // reset sender's counter back to zero
	    // printf("Got living confirmation from (%s)\n", newpacket->sender);
	    
	    client_t* orig_sender = find_first_client_by_username(newpacket->sender);
	    orig_sender->missed_checkups = 0;
	  }
	  else
	  {
	    printf("\nUnrecognized value in checkup message!\n");
	  }
    
	  free_packet(newpacket);
	  break;
	case ELECTION:

	  free(newpacket);
	  break;
	case VOTE:

	  free_packet(newpacket);
	  break;
	case VICTORY:

	  free_packet(newpacket);
	  break;
	case JOIN_REQUEST:
	  //message from someone who wants to join

	  strcpy(newip,strtok(newpacket->packetbody,":"));
	  newport = atoi(strtok(NULL,IPPORTSTRDELIM));
	  printf("Receiving JOIN_REQUEST from: %s:%d\n",newip,newport);
	  client_t* newguy = create_client(newpacket->sender,newip,newport,FALSE);
	  
	  char marshalledaddresses[MAXCHATMESSAGELEN];

	  char uid[MAXUIDLEN];
	  get_new_uid(uid);
	  
	  //if I'm not the leader, send a LEADER_INFO
	  if(!me->isleader)
	  {
	    curr = CLIENTS->head;
	    pthread_mutex_lock(&CLIENTS->mutex);
	    client_t* leader = NULL;
	    while(curr != NULL)
	    {
	      client_t* client = (client_t*)curr->elem;
	      if(client->isleader)
	      {
		leader = client;
		break;
	      }
	      curr = curr->next;
	    }
	    if(leader == NULL)
	      printf("Can't process JOIN_REQUEST. Don't know who the leader is!!!\n");

	    pthread_mutex_unlock(&CLIENTS->mutex);
	    strcpy(marshalledaddresses,leader->username);
	    strcat(marshalledaddresses,":");
	    strcat(marshalledaddresses,leader->hostname);
	    char portbuf[10];
	    sprintf(portbuf,":%d",leader->portnum);
	    strcat(marshalledaddresses,portbuf);

	    printf("Sending LEADER_INFO to %s:%d\n",newguy->hostname,newguy->portnum);
	    send_UDP(LEADER_INFO,leader->username,leader->uid,uid,marshalledaddresses,newguy);
	  }
	  else //I'm the leader, so I can send JOIN
	  {
	    //	  printf("JOIN_REQUEST packet: %s\n",newpacket->packetbody);
	    
	    strcpy(marshalledaddresses,newguy->username);
	    strcat(marshalledaddresses,":");
	    strcat(marshalledaddresses,newguy->hostname);
	    char portbuf[10];
	    sprintf(portbuf,":%d",newguy->portnum);
	    strcat(marshalledaddresses,portbuf);
	    pthread_mutex_lock(&CLIENTS->mutex);
	    curr = CLIENTS->head;
	    while(curr != NULL)
	    {
	      strcat(marshalledaddresses,":");
	      strcat(marshalledaddresses,((client_t*)curr->elem)->username);
	      strcat(marshalledaddresses,":");
	      strcat(marshalledaddresses,((client_t*)curr->elem)->hostname);
	      //	    char portbuf[10];
	      sprintf(portbuf,":%d",((client_t*)curr->elem)->portnum);
	      strcat(marshalledaddresses,portbuf);
	      curr = curr->next;
	    }
	    pthread_mutex_unlock(&CLIENTS->mutex);
	    //	  printf("JOIN info:\t%s\n",marshalledaddresses);

	    printf("Sending JOIN to %s:%d\n",newguy->hostname,newguy->portnum);
	    send_UDP(JOIN,me->username,me->uid,uid,marshalledaddresses,newguy);
	    multicast_UDP(JOIN,me->username,me->uid,uid,marshalledaddresses);
	  }
	  free(newguy);
	  free_packet(newpacket);
	  break;
	  //	case EXIT:
	  //		remove_client(me->hostname,me->portnum);
	case LEADER_INFO:
	  //if someone asked to join, but they didn't ask the leader, instead of sending a JOIN, send them this. 
	  //If you receive this, repeat the JOIN_REQUEST, but to the leader. 
	  strtok(newpacket->packetbody,IPPORTSTRDELIM);
	  strcpy(newip,strtok(NULL,IPPORTSTRDELIM));
	  newport = atoi(strtok(NULL,IPPORTSTRDELIM));
	  printf("Receiving LEADER_INFO: %s:%d\n",newip,newport);
	  client_t* leader = create_client(newpacket->sender,newip,newport,FALSE);
	  join_chat(leader);

	  free_packet(newpacket);
	  break;
	case JOIN:

	  //figure out if this corresponds to an existing chatmessage
	  message = find_chatmessage(newpacket->uid);
	  message = process_packet(message,newpacket);

	  if(message->iscomplete)
	  {
	    //announcement that someone has successfully joined
	    //	    printf("SOMEBODY JOINING!\t%s\n",message->messagebody);

	    //read the first one off first
	    strcpy(newusername,strtok(message->messagebody,":"));
	    strcpy(newip,strtok(NULL,":"));
	    newport = atoi(strtok(NULL,IPPORTSTRDELIM));
	  
	    client_t* newclient = add_client(newusername,newip,newport,FALSE);

	    if(newport == LOCALPORT && strcmp(LOCALHOSTNAME,newip) == 0) //then I'm the guy who just joined
	      {
		me = newclient;

		int usernum = 1;
		while(1)
		  {
		    char* newusertest = strtok(NULL,":");
		    if(newusertest == NULL)
		      break;
		    strcpy(newusername,newusertest);
		    strcpy(newip,strtok(NULL,":"));
		    newport = atoi(strtok(NULL,IPPORTSTRDELIM));
		    if(usernum == 1)
		    {
		      print_info_with_senderids(newusername,"has approved your join request",newip,newport);
		    add_client(newusername,newip,newport,TRUE);
		    }
		    else
		      add_client(newusername,newip,newport,FALSE);

		    usernum++;
		  }

	      }
	  }

	  if(message->iscomplete && me->isleader)
	    assign_sequence(message);

	  process_late_sequence(message, newpacket);
	  
	  free_packet(newpacket);
	  break;
	default:
	  printf("\nUnrecognized packet type: %d\n", newpacket->packettype);
	  free_packet(newpacket);
	}

	//begin sequencing stuff from 
	/*
        
        msg_recv* message_got = parseMessage(&buf);
        
        enqueue(queue, message_got);
        
        msg_recv* next_message_got = dequeue(queue);
        
        if (squence = -1) {
            squence = (*next_message_got).seq_num;
        }
        else if((*next_message_got).seq_num > squence){
            int targetMessage = (*next_message_got).seq_num;
            squence ++;
        
            while (squence <targetMessage) {
            
                printf("redelivery of messages is requested");
            
                next_message_got = retry(&squence);
            
                printf("%s: %s\n", (*next_message_got).user_sent, (*next_message_got).msg_sent);
            
                squence++;
            }
        }
        
        squence ++;
        printf("%s: %s\n", (*next_message_got).user_sent, (*next_message_got).msg_sent);
	*/    
    }//end of while
    pthread_exit((void *)t);
}

// incomplete
// discover IP address using name
void getLocalIp(char *buf){
    
    bzero(buf,1024);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    
    // connect?
    
    //    struct sockaddr_in sockname;
    //    socklen_t socknamelen = sizeof(sockname);
    //    int err = getsockname(sock, (struct sockaddr*) &sockname, &socknamelen);


    //    const char* p = inet_ntop(AF_INET, &sockname.sin_addr, buf, INET_ADDRSTRLEN);
    close(sock);
    return;
}

void send_UDP(packettype_t packettype, char sender[], char senderuid[], char uid[], char messagebody[], client_t* sendtoclient)
{
    
  char packetbodybuf[MAXPACKETBODYLEN];
  char packetbuf[MAXPACKETLEN];
    
  char buf[MAXCHATMESSAGELEN];
  bzero(buf,MAXCHATMESSAGELEN);
    
  struct sockaddr_in other_addr;
  int fd;
  if ((fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)) < 0) {
    fprintf(stderr, "SOCKET CREATION FAILED");
    exit(1);
  }

  int totalpacketsrequired = (strlen(messagebody)) / 815;
  int remainder =  strlen(messagebody) % MAXPACKETBODYLEN;
  if(remainder > 0)
    totalpacketsrequired++;
    
  /* set up destination address,where some client is listening or waiting to rx packets */
  memset(&other_addr,0,sizeof(other_addr));
  other_addr.sin_family=AF_INET;
  other_addr.sin_port=htons(sendtoclient->portnum);
    
  //  if (inet_aton(((client_t*)curr->elem)->hostname, &other_addr.sin_addr)==0) { //check2
  //    fprintf(stderr, "inet_aton() failed\n");
  //    exit(1);
  //  }
  
  pthread_mutex_lock(&messaging_mutex);
  int messageindex = 0;
  int i = 0;
  for(i = 0; i < totalpacketsrequired; i++)
  {
    strncpy(packetbodybuf, messagebody+messageindex, MAXPACKETBODYLEN);
    messageindex += MAXPACKETBODYLEN;

    sprintf(packetbuf, "%s%s%s%s%s%s%d%s%d%s%d%s%s", sender, PACKETDELIM, senderuid, PACKETDELIM, uid, PACKETDELIM, packettype, PACKETDELIM, i, PACKETDELIM, totalpacketsrequired, PACKETDELIM, packetbodybuf);
    //    sprintf(packetbuf, "%s%s%s%s%d%s%d%s%d%s%s", sender, PACKETDELIM, uid, PACKETDELIM, packettype, PACKETDELIM, i, PACKETDELIM, totalpacketsrequired, PACKETDELIM, packetbodybuf);
    //	  printf("now sending %s to %s:%d\n",packetbuf, ((client_t*)curr->elem)->hostname, ((client_t*)curr->elem)->portnum);
    if (sendto(fd, packetbuf, sizeof(packetbuf), 0, (struct sockaddr *) &other_addr, sizeof(other_addr)) < 0) {
      fprintf(stderr, "sendto");
      exit(1);
    }
  }
  pthread_mutex_unlock(&messaging_mutex);
}

void multicast_UDP(packettype_t packettype, char sender[], char senderuid[], char uid[], char messagebody[]){
    
    struct sockaddr_in addr;
    int fd;
    //    printf("prepping to send\n");

    int totalpacketsrequired = (strlen(messagebody)) / 815; 
    int remainder =  strlen(messagebody) % MAXPACKETBODYLEN; 
    if(remainder > 0)
      totalpacketsrequired++;
    
    fd = socket(PF_INET,SOCK_DGRAM,0);
    
    if (fd < 0) {
        fprintf(stderr,"socket create failed");
        exit(1);
    }
    
    node_t* curr = CLIENTS->head;
    char packetbodybuf[MAXPACKETBODYLEN];
    char packetbuf[MAXPACKETLEN];

    //    printf("total packets required: %d\n", totalpacketsrequired);
    pthread_mutex_lock(&messaging_mutex);
    while(curr != NULL)
    {
        memset(&addr, 0, sizeof(addr));
        addr.sin_family=AF_INET;
        addr.sin_port=htons(((client_t*)curr->elem)->portnum);
        
	if (inet_aton(((client_t*)curr->elem)->hostname, &addr.sin_addr)==0) {
	  fprintf(stderr, "inet_aton() failed\n");
	  exit(1);
	}


	int messageindex = 0;
	int i;
	for(i = 0; i < totalpacketsrequired; i++)
	{
	  strncpy(packetbodybuf, messagebody+messageindex, MAXPACKETBODYLEN);
	  messageindex += MAXPACKETBODYLEN;

	  sprintf(packetbuf, "%s%s%s%s%s%s%d%s%d%s%d%s%s", sender, PACKETDELIM, senderuid, PACKETDELIM, uid, PACKETDELIM, packettype, PACKETDELIM, i, PACKETDELIM, totalpacketsrequired, PACKETDELIM, packetbodybuf);
	  //	  printf("now sending %s to %s:%d\n",packetbuf, ((client_t*)curr->elem)->hostname, ((client_t*)curr->elem)->portnum);
	  if (sendto(fd, packetbuf, sizeof(packetbuf), 0, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
            fprintf(stderr, "sendto");
            exit(1);
	  }
	}

	curr = curr->next;
    }
    pthread_mutex_unlock(&messaging_mutex);
    //close(fd);
}

