#include "dchat.h"

void print_client_list();
client_t* create_client(char username[], char hostname[], int portnum, bool isleader);
client_t* add_client(char username[], char hostname[], int portnum, bool isleader);
void remove_client(char hostname[], int portnum);
client_t* find_first_client_by_username(char username[]);
client_t* find_client_by_uid(char uid[]);

void holdElection();
