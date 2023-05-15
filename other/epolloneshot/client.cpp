#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <assert.h>
#include <string.h>

int main() {
    // 1. 创建套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(sockfd != -1);
    // 2. 向服务器（特定的IP和端口）发起请求
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr)); // 每个字节都用0填充
    serv_addr.sin_family = AF_INET; // 使用IPV4地址
    int ret = inet_pton(AF_INET, "192.168.31.238", &serv_addr.sin_addr.s_addr);
    assert(ret != -1);
    serv_addr.sin_port = htons(8000);

    ret = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(sockaddr));
    assert(ret != -1);

    char buf[1024] = "nihao, i am client\n";
    write(sockfd, buf, strlen(buf) + 1);

    int len = read(sockfd, buf, sizeof(buf));
    if (len == -1) {
        perror("read");
        return -1;
    } else if (len > 0) {
        printf("read buf = %s\n", buf);
    } else {
        printf("服务器已经断开连接...\n");
    }
    
    close(sockfd);
    return 0;
}