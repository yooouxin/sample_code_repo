#include <iostream>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <map>

struct client_data
{
    int id;
    sockaddr_in address;
    char* write_buf;
    char buf[1024];
};

const int kUserLimit = 5;


int set_nonblocking(int fd){
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}


int main(int argc,char *argv[]) {

    int port = atoi(argv[1]);
    printf("listen on %d\n",port);
    int listen_fd = socket(PF_INET,SOCK_STREAM,0);
    if(listen_fd <0 ){
        return -1;
    }
    int opt = 1;
    setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    sockaddr_in my_address;
    bzero(&my_address,sizeof(my_address));
    my_address.sin_port = htons(port);
    my_address.sin_family = AF_INET;
    my_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(bind(listen_fd, reinterpret_cast<const sockaddr *>(&my_address),
         sizeof(my_address)) <0){
        return -1;
    }

    if(listen(listen_fd,5) <0){
        return -1;
    }

    std::map<int,client_data> users;
    int ep_fd = epoll_create(1);
    if(ep_fd <0){
        return -1;
    }


    epoll_event listen_ev;
    listen_ev.data.fd = listen_fd;
    listen_ev.events = EPOLLIN | EPOLLERR;
    epoll_ctl(ep_fd,EPOLL_CTL_ADD,listen_fd,&listen_ev);

    int user_cnt = 0;
    while(1){
        epoll_event events[user_cnt+1]; //user_limit + listen
        int ev_cnt = epoll_wait(ep_fd,events,
                                user_cnt+1,-1);
        if(ev_cnt < 0){
            break;
        }

        for (int i = 0; i <ev_cnt; ++i) {
            if(events[i].data.fd == listen_fd){
                if(events[i].events & EPOLLIN){
                    sockaddr_in client_address;
                    socklen_t len = sizeof(client_address);
                    int connected_fd = accept(listen_fd, reinterpret_cast<sockaddr *>
                        (&client_address), &len);
                    if(connected_fd <0){
                        perror("can not accept connection\n");
                        continue;
                    }
                    printf("New client connected : %d\n",user_cnt);
                    users[connected_fd].id = user_cnt;
                    users[connected_fd].address = client_address;
                    user_cnt++;

                    set_nonblocking(connected_fd);
                    epoll_event new_ev;
                    new_ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
                    new_ev.data.fd = connected_fd;
                    epoll_ctl(ep_fd,EPOLL_CTL_ADD,connected_fd,&new_ev);
                }else{
                    continue;
                }
            }else{
                //connected fd
                if(events[i].events & EPOLLERR){
                    continue;
                }

                if(events[i].events & EPOLLRDHUP){
                    //delete user
                    users.erase(events[i].data.fd);
                    user_cnt--;
                    epoll_ctl(ep_fd,EPOLL_CTL_DEL,events[i].data.fd, nullptr);
                    close(events[i].data.fd);
                }else if(events[i].events & EPOLLIN){
                    //read data
                    int conn_fd = events[i].data.fd;
                    bzero(users[conn_fd].buf,1024);
                    int bytes = recv(conn_fd,users[conn_fd].buf,1024,0);
                    if(bytes <0){
                        if(errno != EAGAIN){
                            //delete user
                            users.erase(events[i].data.fd);
                            user_cnt--;
                            epoll_ctl(ep_fd,EPOLL_CTL_DEL,events[i].data.fd, nullptr);
                            close(events[i].data.fd);
                        }
                    }else if(bytes == 0) {
                        //client disconnect
                        //delete user
                        users.erase(events[i].data.fd);
                        user_cnt--;
                        epoll_ctl(ep_fd,EPOLL_CTL_DEL,events[i].data.fd, nullptr);
                        close(events[i].data.fd);
                    }else {
                            printf("get %d bytes from %d : %s \n",
                                   bytes, users[conn_fd].id,users[conn_fd].buf);
                            //call other client need send data
                            for (auto &user:users) {
                                if(user.first == conn_fd){
                                    continue;
                                }
                                epoll_event ev;
                                ev.data.fd = user.first;
                                ev.events = EPOLLOUT;
                                epoll_ctl(ep_fd,EPOLL_CTL_MOD,user.first,&ev);
                                user.second.write_buf = users[conn_fd].buf;
                            }
                    }
                }else{
                    //can output
                    int conn_fd = events[i].data.fd;
                    if(!users[conn_fd].write_buf){
                        continue;
                    }
                    printf("Send message to client %d\n",users[conn_fd].id);
                    int bytes = send(conn_fd,users[conn_fd].write_buf, strlen(users[conn_fd].write_buf),0);
                    if(bytes <0 ){
                        printf("can not send to other client\n");
                    }
                    users[conn_fd].write_buf = nullptr;

                    epoll_event ev;
                    ev.data.fd = conn_fd;
                    ev.events = EPOLLIN;
                    epoll_ctl(ep_fd,EPOLL_CTL_MOD,conn_fd,&ev);

                }
            }
        }
    }



    return 0;
}
