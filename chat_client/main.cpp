#include <iostream>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

int main(int argc,char * argv[]) {
    if(argc <=1){
        printf("usage : port_number\n");
        return 1;
    }

   int port = atoi(argv[1]);
//    int port =12345;
    printf("port number : %s\n",argv[1]);

    int sock_fd = socket(PF_INET,SOCK_STREAM,0);
    if(sock_fd <0){
        printf("Open socket error\n");
        return -1;
    }

    sockaddr_in server_address;
    bzero(&server_address,sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);
    if(connect(sock_fd, reinterpret_cast<const sockaddr *>(&server_address),
               sizeof(server_address)) < 0){
        printf("Can not connect to server\n");
        return -1;
    }

    printf("Connected\n");

    int ep_fd = epoll_create(1);
    if(ep_fd <0){
        return -1;
    }
    epoll_event events[2];
    events[0].data.fd = 0;
    events[0].events = EPOLLIN;

    events[1].data.fd = sock_fd;
    events[1].events = EPOLLIN;

    epoll_ctl(ep_fd,EPOLL_CTL_ADD,0,&events[0]);
    epoll_ctl(ep_fd,EPOLL_CTL_ADD,sock_fd,&events[1]);
    int pipe_fd[2];
    if(pipe(pipe_fd) < 0){
        return -1;
    }

    epoll_event events_wait[2];
    while(1){
        if(epoll_wait(ep_fd,events_wait,2,-1) < 0){
            printf("Wait epoll error\n");
            break;
        }

        char buffer[1024];
        int user_input_bytes;
        for(int i=0;i<2;i++){
            //stdin input event
            if(events_wait[i].data.fd == 0 && events_wait[i].events & EPOLLIN){
                user_input_bytes = read(0,buffer,1024);
                epoll_event ev;
                ev.data.fd = sock_fd;
                ev.events = EPOLLOUT;
                epoll_ctl(ep_fd,EPOLL_CTL_MOD,sock_fd,&ev);
            }else if(events_wait[i].data.fd ==  sock_fd && events_wait[i].events & EPOLLRDHUP){
                printf("Server disconnect\n");
                break;
            }else if(events_wait[i].data.fd ==  sock_fd && events_wait[i].events & EPOLLIN){
                printf("Receive event\n");
                memset(buffer,0,1024);
                int bytes = recv(sock_fd,buffer,1024,0);
                if(bytes == 0){
                    printf("Server disconnect\n");
                    goto exit;
                }else if(bytes < 0){
                    printf("Something error\n");
                    goto exit;
                }
                printf("Get %d bytes : %s \n",bytes,buffer);
            }else if(events_wait[i].data.fd ==  sock_fd && events_wait[i].events & EPOLLOUT){
                //out
                int bytes = send(sock_fd,buffer, user_input_bytes,0);
                printf("Send %d bytes to server\n",bytes);
                epoll_event ev;
                ev.data.fd = sock_fd;
                ev.events = EPOLLIN;
                epoll_ctl(ep_fd,EPOLL_CTL_MOD,sock_fd,&ev);
            }
        }
    }

exit:
    close(sock_fd);
    return 0;
}
