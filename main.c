//
//  main.c
//  sshusbmux
//
//  Created by Никита Пупышев on 24.03.15.
//
//

#include <stdio.h>
#include <stdlib.h>
#include "MobileDevice.h"
#include <netinet/in.h>
#include <spawn.h>
#include <sys/stat.h>

extern char **environ;
AMDeviceRef device = NULL;
int tcp_server_socket = -1;
int start_tcp_server(int port);
int server_port = 31765;
int device_port = 22;
bool die_on_disconnect = false;
bool tunneling_mode = false;
void start_ssh(int port);
void tie_in_sockets(int, int);
void accept_connection(int server_socket, void(^handler)(int));
void connection_callback(struct am_device_notification_callback_info*, void*);
void help(void);

int main(int argc, const char * argv[]) {
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "-t", 2)) tunneling_mode = true;
        else if (!strncmp(argv[i], "--die", 10)) die_on_disconnect = true;
        else if (!strncmp(argv[i], "-p", 2) && ((argc - i) > 1)) {
            server_port = atoi(argv[i+1]);
        }
        else if (!strncmp(argv[i], "-d", 2) && ((argc - i) > 1)) {
            device_port = atoi(argv[i+1]);
        }
        else if (!strncmp(argv[i], "-h", 2)) {
            help();
            return 0;
        }
    }
    
    struct am_device_notification *notification;
    if (AMDeviceNotificationSubscribe(&connection_callback, 0, 0, 0, &notification) == MDERR_OK) {
        //TCP server is running for the whole program life cycle
        if ((tcp_server_socket = start_tcp_server(server_port)) > 0) {
            CFRunLoopRun();
        }
        else printf("[!] Failed to start TCP server on port %i.\n", server_port);
    }
    return 0;
}

//We cannot just get a am_device struct so we use CFRunLoopRun() and callbacks
//P.S. I don't think -v (verbose) flag is a good idea for such a simple tool
void connection_callback(struct am_device_notification_callback_info *info, void* arg) {
    switch (info->msg) {
        case ADNCI_MSG_CONNECTED:
            if (!device) {
                if (AMDeviceConnect(info->dev) == MDERR_OK) {
                    //connected to device
                    if (AMDeviceGetInterfaceType(info->dev) == 1) {
                        //checked if device is connected by USB
                        device = info->dev;
                        
                        int device_socket;
                        muxconn_t connection_id = AMDeviceGetConnectionID(device);
                        //Got USBMux connection ID
                        
                        if (USBMuxConnectByPort(connection_id, htons(22), &device_socket) == MDERR_OK) {
                            //Connected to device
                            void(^handler)(int) = ^(int client_socket) {
                                tie_in_sockets(client_socket, device_socket);
                            };
                            
                            //I just like this stuff
                            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                                if (tunneling_mode) {
                                    while (device) {
                                        accept_connection(tcp_server_socket, handler);
                                    }
                                }
                                else accept_connection(tcp_server_socket, handler);
                                close(device_socket);
                            });
                            
                            if (!tunneling_mode) {
                                dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                                    start_ssh(server_port);
                                    //start_ssh is a synchronous method so run loop is stopped
                                    //only after it has been executed
                                    CFRunLoopStop(CFRunLoopGetMain());
                                });
                            }
                        }
                        else printf("[!] Cannot connect to device port %i.\n", device_port);
                    }
                    else puts("[!] Wrong interface type.");
                }
                else puts("[!] Failed to connect to iOS device.");
            }
            break;
            
        case ADNCI_MSG_DISCONNECTED:
            if (device) {
                if (info->dev->device_id == device->device_id) {
                    device = NULL;
                    puts("\n\r[!] Device has been disconnected.\r");
                    if (die_on_disconnect) {
                        close(tcp_server_socket);
                        CFRunLoopStop(CFRunLoopGetMain());
                    }
                }
            }
            break;
            
        default:
            break;
    }
}

//Starts ssh and connects to our server.
//Function is small enough and is called only once. Why not make it inline?
inline void start_ssh(int port) {
    //Yes, I've counted them all and yes, I know that %i is not 2, but 5 chars
    char *buffer = (char *)malloc(56 * sizeof(char));
    sprintf(buffer, "ssh -p %i -o StrictHostKeyChecking=no root@localhost", server_port);
    //It's convinient to use system() call here
    system(buffer);
    
    free(buffer);
}

//Accepts connection from device
void accept_connection(int server_socket, void(^handler)(int client_socket)) {
    int client_socket;
    socklen_t length;
    struct sockaddr_in client_addr;
    
    length = sizeof(client_addr);
    client_socket = accept(server_socket,
                           (struct sockaddr *) &client_addr,
                           &length);
    
    if (client_socket < 0) {
        puts("[!] Failed to accept connection from device.");
        return;
    }
    
    
    handler(client_socket);
    close(client_socket);
}

//Relays data between sockets
void tie_in_sockets(int socket1, int socket2) {
    fd_set read_set;
    
    struct timeval timeout;
    timeout.tv_usec = 0;
    timeout.tv_sec  = 260;
    
    const long buffer_size = 1024;
    char* buffer = (char*)malloc(buffer_size);
    
    if(!buffer)
        return;
    
    long(^redirect)(int,int,fd_set*) = ^(int s1, int s2, fd_set* set) {
        if(FD_ISSET(s1, set)) {
            //just redirect all data from socket 1 to socket 2
            ssize_t bytes_recvd = recv(s1, buffer, buffer_size, 0);
            if(bytes_recvd <= 0) {
                return (long)-1;
            }
            
            char* p = buffer;
            ssize_t overall_bytes_sent = 0;
            
            while(overall_bytes_sent != bytes_recvd) {
                ssize_t bytes_sent = write(s2, p, bytes_recvd - overall_bytes_sent);
                
                if(bytes_sent < 0)
                    return (long) -1;
                
                overall_bytes_sent += bytes_sent;
                p+=bytes_sent;
            }
        }
        return (long)0;
    };
    
    FD_ZERO(&read_set);
    
    int activity = 1;
    //No logs in SSH client mode
    if (tunneling_mode) printf("Relaying data to device port %i...\n", device_port);
    while(errno != EINTR) {
        FD_SET(socket1, &read_set);
        FD_SET(socket2, &read_set);
        
        activity = select(FD_SETSIZE, &read_set, NULL, NULL, &timeout);
        
        if(activity >= 0) {
            
            long res = 0;
            
            res = redirect(socket1,socket2,&read_set);
            
            if(res != 0)
                break; //socket has been disconnected
            
            res = redirect(socket2,socket1,&read_set);
            
            if(res != 0)
                break; //socket has been disconnected
            
        }
    }
    if (tunneling_mode) printf("Connection to device port %i is closed.\n", device_port);
    
    free(buffer);
}

//Starts a TCP server. I didn't like the idea of implementing all the SSH stuff
//By myself so I just connect ssh client to my server
int start_tcp_server(int port) {
    int server_socket;
    struct sockaddr_in server_addr;
    
    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket <= 0) return -1;
    
    bzero((char *) &server_addr, sizeof(server_addr));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        close(server_socket);
        return -1;
    }
    
    if(listen(server_socket, 5) !=0) {
        close(server_socket);
        return -1;
    }
    
    return server_socket;
}

void help() {
    puts("Description: sshusbmux starts a TCP server, ties it to iOS");
    puts("device SSH server via USBMux and opens ssh shell if nessesary.");
    puts("Strict key verification is disabled automatically if utility");
    puts("isn't running in tunneling mode (\"-t\" option).\n");
    puts("Usage: sshusbmux [options]");
    puts("Options:");
    puts("\t-p <port>  Set local port. Default 31765.");
    puts("\t-d <port>  Set remote port. Default 22.");
    puts("\t-t         Tunneling mode. Instead of opening SSH shell");
    puts("\t           just relays data between local and remote ports.");
    puts("\t--die      If device is disconnected, server will die.");
    puts("\t-h         Shows this help message.");
}
