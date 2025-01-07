#ifndef _PEER_H_
#define _PEER_H_

#include "utils.h"



void send_data_to_tracker(ClientFiles_t* client);

void read_from_file(ClientFiles_t* client, int rank);

void free_client_files(ClientFiles_t* cf);



#endif