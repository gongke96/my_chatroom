#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <poll.h>
#include <vector>
#include <unordered_map>
#include <algorithm>

using namespace std;

#define USER_LIMIT 5    //最大用户数量
#define BUFFER_SIZE 64  //读缓冲区的大小
#define FD_LIMIT 65535  //文件描述符数量限制

//客户数据：客户端socket地址、待写到客户端的数据的位置、从客户端读入的数据
struct client_data
{
    sockaddr_in address;
    char* write_buf;
    char buf[ BUFFER_SIZE ];
};

//将文件描述符设置为非阻塞的
int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

//vector的删除操作

void vectorerase( int fd, vector<int>& v)
{
    for( auto iter = v.begin(); iter != v.end(); )
    {
        if( *iter == fd)
        {
            iter = v.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}

int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    //创建users数组，分配FD_LIMIT个client_data对象。
    client_data* users = new client_data[FD_LIMIT];
    //尽管分配了足够多的client_data对象，但为了提高poll的性能，仍然有必要限制用户的数量
    pollfd fds[USER_LIMIT+1];
    int user_counter = 0;
    for( int i = 1; i <= USER_LIMIT; ++i )
    {
        fds[i].fd = -1;
        fds[i].events = 0;
    }
    fds[0].fd = listenfd;
    fds[0].events = POLLIN | POLLERR;
    fds[0].revents = 0;

    unordered_map<string,vector<int>>room_map;
    unordered_map<int,string>user_map;                                                            

    while( 1 )
    {
        ret = poll( fds, user_counter+1, -1 );
        if ( ret < 0 )
        {
            printf( "poll failure\n" );
            break;              
        }        
        for( int i = 0; i < user_counter+1; ++i )
        {
            if( ( fds[i].fd == listenfd ) & ( fds[i].revents & POLLIN ) )
            {
                //新加入连接
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                //如果请求太多，则关闭新加的连接
                if( user_counter >= USER_LIMIT )
                {
                    const char* info = "too many users\n";
                    printf( "%s", info );
                    send( connfd, info, strlen( info ), 0 );
                    close( connfd );
                    continue;
                }
                //对于新的连接，修改fds和users数组等数据
                user_counter++;
                users[connfd].address = client_address;
                setnonblocking( connfd );
                fds[user_counter].fd = connfd;
                fds[user_counter].events = POLLIN | POLLRDHUP | POLLERR;
                fds[user_counter].revents = 0;
                room_map["public_room"].push_back(connfd);
                user_map[connfd] = "public_room";
                printf( "comes a new user\n" );
            }
            else if( fds[i].revents & POLLERR )
            {
                //出现错误
                printf( "get an error from %d\n", fds[i].fd );
                char errors[ 100 ];
                memset( errors, '\0', 100 );
                socklen_t length = sizeof( errors );
                if( getsockopt( fds[i].fd, SOL_SOCKET, SO_ERROR, &errors, &length ) < 0 )
                {
                    printf( "get socket option failed\n" );
                }
                continue;
            }
            else if( fds[i].revents & POLLRDHUP )
            {
                //如果客户端关闭连接，则服务器也关闭相应的连接，并将用户数减1
                users[fds[i].fd] = users[fds[user_counter].fd];
                close( fds[i].fd );
                fds[i] = fds[user_counter];
                i--;
                user_counter--;
                vectorerase(fds[i].fd , room_map[user_map[fds[i].fd]]);
                user_map.erase(fds[i].fd);
                printf( "a client left\n" );
            }
            else if( fds[i].revents & POLLIN )
            {
                //数据可读
                int connfd = fds[i].fd;
                memset( users[connfd].buf, '\0', BUFFER_SIZE );
                ret = recv( connfd, users[connfd].buf, BUFFER_SIZE-1, 0 );
                printf( "get %d bytes of client data %s from %d\n", ret, users[connfd].buf, connfd );
                if( ret < 0 )
                {
                    //如果读出错，则关闭连接
                    if( errno != EAGAIN )
                    {
                        close( connfd );
                        users[fds[i].fd] = users[fds[user_counter].fd];
                        fds[i] = fds[user_counter];
                        i--;
                        user_counter--;
                    }
                }
                else if( ret == 0 )
                {
                    printf( "code should not come to here\n" );
                }
                else
                {
                    //判断是否为加入房间请求
                    string str_buf = users[connfd].buf;
                    if( str_buf.substr(0,15) == "Enter the room:")
                    {
                        string room_id = str_buf.substr(15);
                        room_map[room_id].push_back(fds[i].fd);
                        vectorerase(fds[i].fd,room_map["public_room"]);
                        user_map[fds[i].fd] = room_id;
                        printf( "client %d enter %s\n", connfd, room_id.c_str() );
                        continue;
                    }
                    else if( str_buf.substr(0,14) == "Quit the room:" )
                    {
                        string room_id = str_buf.substr(14);
                        if( find(room_map[room_id].begin(),room_map[room_id].end(),connfd) != room_map[room_id].end() )
                        {
                            room_map["public_room"].push_back(fds[i].fd);
                            vectorerase(fds[i].fd,room_map[room_id]);
                            user_map[fds[i].fd] = "public_room";
                            printf( "client %d left %s\n", connfd, room_id.c_str() );
                            continue;
                        }
                        else
                        {
                            fds[i].events |= ~POLLIN;
                            fds[i].events |= POLLOUT;
                            char str_buf[33] = "Warning:You are not in this room";
                            users[connfd].write_buf = str_buf;
                        }
                    }
                    else
                    {
                        //如果接收到指定房间的客户数据，则通知房间内其它socket连接准备写数据
                        string room_id = user_map[fds[i].fd];
                        for( auto& j : room_map[room_id] )
                        {
                            if( j == connfd )
                            {
                                continue;
                            }
                            int k = 0;
                            while( fds[k].fd != j )
                            {
                                ++k;
                            }
                            fds[k].events |= ~POLLIN;
                            fds[k].events |= POLLOUT;
                            users[j].write_buf = users[connfd].buf;
                        }
                    }
                }
            }
            else if( fds[i].revents & POLLOUT )
            {
                //数据可写
                int connfd = fds[i].fd;
                if( !users[connfd].write_buf )
                {
                    continue;
                }
                ret = send( connfd, users[connfd].write_buf, strlen( users[connfd].write_buf ), 0 );
                users[connfd].write_buf = NULL;
                //写完数据后需要重新注册fds[i]上的可读事件
                fds[i].events |= ~POLLOUT;
                fds[i].events |= POLLIN;
            }
        }
    }

    delete [] users;
    close( listenfd );
    return 0;
}