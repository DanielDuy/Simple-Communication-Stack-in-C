#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <errno.h>

#include "l2sap.h"


/* compute_checksum is a helper function for l2_sendto and
 * l2_recvfrom_timeout to compute the 1-byte checksum both
 * on sending and receiving and L2 frame.
 */
static uint8_t compute_checksum( const uint8_t* frame, int len ) {
    // Variable to store result of computing checksum
    uint8_t checksum = 0;

    // For-loop that goes through the whole L4 Frame
    int i;
    for (i = 0; i < len; i++) {

        // This makes sure the checksum variable on the header isnt computated with the rest of the L4 Frame
        if (i != offsetof(L2Header, checksum)) {
            checksum ^= frame[i];
        }
    }
    return checksum;
}

L2SAP* l2sap_create( const char* server_ip, int server_port )
{   
    // Creating variables for our socket
    struct sockaddr_in dest_addr;
    // Filling the memory occupied by the structure with 0's
    memset(&dest_addr, 0, sizeof(dest_addr));
    struct in_addr ip_addr;

    // Creaiting a socket with error handling if it fails to do so
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        perror("create new socket failed in l2sap_create()");
        return NULL;
    }

    // Converts ip address to binary and stores it in the structure ip_addr with error handling if it fails    
    int wc = inet_pton(AF_INET, server_ip, &ip_addr.s_addr);
    if (!wc) {
        fprintf(stderr, "in l2sap_create(), invalid IP adress: %s\n", server_ip);
        return NULL;
    }

    // Setting up sockaddr_in with address family, port, IP adress
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(server_port);
    dest_addr.sin_addr = ip_addr;

    // Allocating memory for L2SAP with error handling
    struct L2SAP *newL2SAP = (L2SAP*)malloc(sizeof(L2SAP));
    if (!newL2SAP) {
        perror("malloc for new L2SAP failed in l2sap_create()");
        close(fd);
        return NULL;
    }

    // Assigning thew new L2SAP with socket and address
    newL2SAP->socket = fd;
    newL2SAP->peer_addr = dest_addr;

    return newL2SAP;
}

void l2sap_destroy(L2SAP* client)
{
    // If client isn't NULL / exists
    if (client) {
        // Closing socket
        close(client->socket);
        // Free allocated memory
        free(client);
    }
}

/* l2sap_sendto sends data over UDP, using the given UDP socket
 * sock, to a remote UDP receiver that is identified by
 * peer_address.
 * The parameter data points to payload that L3 wants to send
 * to the remote L3 entity. This payload is len bytes long.
 * l2_sendto must add an L2 header in front of this payload.
 * When the payload length and the L2Header together exceed
 * the maximum frame size L2Framesize, l2_sendto fails.
 */
int l2sap_sendto( L2SAP* client, const uint8_t* data, int len )
{
    // Checking if payload size combined with L2 header size is bigger than maximum allowed L2 Frame size
    if (sizeof(struct L2Header) + len > L2Framesize) {
        fprintf(stderr, "data in payload is greater than max L2Framesize\n");
        return -1;
    }

    // Buffer to store the payload and the header that will be sent
    uint8_t buff[L2Framesize];
    // Filling the memory occupied by the structure with 0's
    memset(buff, 0, sizeof(buff));

    // Creating a new L2 Header inside the buffer
    L2Header *newL2Header = (L2Header*)buff;

    // Setting up L2 Header
    newL2Header->dst_addr = client->peer_addr.sin_addr.s_addr;
    newL2Header->len = htons(len + sizeof(L2Header));
    newL2Header->checksum = 0;
    newL2Header->mbz = 0;

    // Copying the payload data to the buffer, after the L2 Header 
    memcpy(buff + sizeof(L2Header), data, len);

    // Computes the checksum for the whole L2 Frame (not including checksum variable) and stores the result in the header
    newL2Header->checksum = compute_checksum(buff, sizeof(L2Header) + len);

    // Sending the newly created L2 Frame to the socket
    int wc = sendto(
        client->socket, 
        buff, 
        sizeof(L2Header) + len, 
        0, 
        (struct sockaddr*)&client->peer_addr, 
        sizeof(struct sockaddr_in)
    );
    if (wc == -1) {
        perror("sendto failed in l2sap_sendto()");
        return -1;
    }

    return 0;
}

/* Convenience function. Calls l2sap_recvfrom_timeout with NULL timeout
 * to make it waits endlessly.
 */
int l2sap_recvfrom( L2SAP* client, uint8_t* data, int len )
{
    return l2sap_recvfrom_timeout(client, data, len, NULL);
}

/* l2sap_recvfrom_timeout waits for data from a remote UDP sender, but
 * waits at most timeout seconds.
 * It is possible to pass NULL as timeout, in which case
 * the function waits forever.
 *
 * If a frame arrives in the meantime, it stores the remote
 * peer's address in peer_address and its size in peer_addr_sz.
 * After removing the header, the data of the frame is stored
 * in data, up to len bytes.
 *
 * If data is received, it returns the number of bytes.
 * If no data is reveid before the timeout, it returns L2_TIMEOUT,
 * which has the value 0.
 * It returns -1 in case of error.
 */
 int l2sap_recvfrom_timeout(L2SAP* client, uint8_t* data, int len, struct timeval* timeout) {
    // File descriptor for the socket
    fd_set fdset;

    // For storing the received socket
    struct sockaddr_in peer_addr;
    socklen_t addrlen = sizeof(peer_addr);

    // Creating a timeval structure to wait for a receiving packet
    struct timeval tv;

    // Creating a buffer with 8192 bytes (may wary) and initializes all to elements to 0's
    char buf[BUFSIZ] = {0};

    // Seting up fdset
    FD_ZERO(&fdset);
    FD_SET(client->socket, &fdset);

    int activity;
    // Wait for data with timeout
    if (timeout) {
        // If a timeval structure is given (not NULL), create a timeout using the timeval
        tv.tv_sec = timeout->tv_sec;
        tv.tv_usec = timeout->tv_usec;
        // Wait for activity on socket until the time expires out
        activity = select(client->socket + 1, &fdset, NULL, NULL, &tv);
    } else {
        // If a timeval structure is not given, it timeout will be set to 0.0000 seconds
        activity = select(client->socket + 1, &fdset, NULL, NULL, NULL);
    }
    
    // Check if select failed or timed out
    if (activity < 0) {
        perror("select");
        return -1;
    } else if (activity == 0) {
        return L2_TIMEOUT;
    }

    // Receiving data from socket with error handling
    int rc = recvfrom(client->socket, buf, sizeof(buf), 0, (struct sockaddr*)&peer_addr, &addrlen);
    if (rc < 0) {
        perror("recvfrom");
        return -1;
    }

    // If the length of the received bytes is less hand L2 Header size, its invalid 
    if (rc < L2Headersize) {
        perror("rc less than L2Headersize");
        return -1; 
    }

    // If the length of the received bytes is the size of a L2 Header, it returns 0
    if (rc == L2Headersize) {
        return 0;
    }

    // Interpreting the start of the buffer as an L2 Header
    L2Header* l2header = (L2Header*)buf;
    
    uint8_t original_checksum = l2header->checksum;
    l2header->checksum = 0;
    // Checking the if checksum is correct
    if (compute_checksum((uint8_t*)buf, rc) != original_checksum) return -1;

    // Setting the peer address from received socket
    memcpy(&client->peer_addr, &peer_addr, sizeof(peer_addr));
    // Copying the received data and storing it to data
    memcpy(data, buf + L2Headersize, rc - L2Headersize);

    // Returns the payload size
    return rc - L2Headersize;
}

