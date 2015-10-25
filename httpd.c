/*
 * 简单Web服务器程序,编译环境Ubuntu15.10
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: httpd/0.1.0\r\n"

void accept_request(int);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/*
 * 客户端请求到来的时候,服务器开启线程处理请求函数
 * @parameters: 已经建立连接的套接字
 */
/**********************************************************************/
void accept_request(int client)
{
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* True表示这是个CGI程序*/
    char *query_string = NULL;

    // 读取http服务器第一行: GET /(url) HTTP/1.1
    numchars = get_line(client, buf, sizeof(buf));
    i = 0;
    j = 0;

    // 取出http请求方法
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';

    // 暂时支持GET和POST方法
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    // 是POST方法说明要调用CGI程序处理
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;

    // 取出url
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    /*
     * 如果GET方法中有查询字符串,取出来
     * 例如: url = /test?x=1&y=2
     * 转换成: /test\0x=1&y=2
     * query_string 就指向x的地址了
     *
     */
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1; // 带查询字符串的当作POST来处理
            *query_string = '\0';
            query_string++;
        }
    }

    // 默认路径是在www下的,和url传入的地址连接起来 www/test
    sprintf(path, "www%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    if (stat(path, &st) == -1)  // 文件获取失败
    {
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        // 如果是dir,默认去文件夹下的index.html
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        /* S_IXUSR（S_IEXEC） 00100 文件所有者具可执行权限
         * S_IXGRP 00010 用户组具可执行权限
         * S_IXOTH 00001 其他用户具可执行权限
         */
        if ( (st.st_mode & S_IXUSR) ||
             (st.st_mode & S_IXGRP) ||
             (st.st_mode & S_IXOTH) )
            cgi = 1;
        if (!cgi) //静态文件
            serve_file(client, path);
        else // CGI脚本
            execute_cgi(client, path, method, query_string);
    }

    close(client);
}

/**********************************************************************/
/* 客户端请求错误返回
 * @parameters: 客户端套接字 */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/*
 * 把文件内容全部发送到哦啊客户端
 * @parameters: 客户端套接字
 */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];
    // 这个地方先要读取,第一次遇到文件结束符时没有置位
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource); // 这个地方读到文件结尾就置位了
    }
}

/**********************************************************************/
/* 通知客户端CGI不可执行
 * @parameter: 客户端套接字. 
 */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/*
 * 使用perror输出错误信息,退出程序
 * @parameter: 错误信息
 */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/*
 * 执行CGI脚本,CGI环境变量要设置好
 * 
 * @parameters client: 客户端套接字
 * @parameters path: 脚本路径
 * @parameters method: HTTP方法
 * @parameters query_string: 查询字符串
 */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
    char buf[1024];

    int fd[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, fd); // 使用socketpair双向管道

    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A';
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
    {
        while ((numchars > 0) && strcmp("\n", buf))  /* 读取并丢弃HTTP头 */
            numchars = get_line(client, buf, sizeof(buf));
    }
    else    /* POST */
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1)
        {
            bad_request(client);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    if ( (pid = fork()) < 0 )
    {
        cannot_execute(client);
        return;
    }
    if (pid == 0)  /* 子进程 */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        close(fd[0]);// 关闭管道的父进程端

        dup2(fd[1], STDIN_FILENO); // 重定向为标准输出,输出最后处理的内容
        dup2(fd[1], STDOUT_FILENO); // 重定向为标准输入,获取POST的内容

        close(fd[1]);

        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0)
        {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else     /* POST */
        {
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        // 如果execl执行成功不返回值,下面的代码就不执行了
        if (execl(path, path, NULL) < 0)
            perror("CGI Script execl failed");
        exit(127); // 失败抛出127
    }
    else        /* parent */
    {
        // 关闭管道的子进程端
        close(fd[1]);

        // 使用POST方法时，WEB服务器通过stdin(标准输入)，向CGI程序传送数据。
        // 服务器 在数据的最后没有使用EOF字符标记，因此程序为了正确的读取stdin
        // 必须使用CONTENT_LENGTH。
        if (strcasecmp(method, "POST") == 0)
        {
            for (i = 0; i < content_length; i++)
            {
                recv(client, &c, 1, 0);
                write(fd[0], &c, 1);
            }
        }
        shutdown(fd[0], SHUT_WR);
        while (read(fd[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(fd[0]);
        waitpid(pid, &status, 0); // 等待子进程结束
    }
}

/**********************************************************************/
/*
 * 从客户端中读取一行,不管这行是以'\r'(回车),'\n'(换行),或者CRLF('\r\n')
 * 回车换行的组合. 读到null的时候终止,如果一直没有换行,到达buff的最大长
 * 就停止读取,加上结束符,上的三种结束符都转换为换行,终止并且加上结束符
 * @parameters sock: 客户端套接字
 * @parameters buf: 存放数据的buff
 * @parameters buf: buff的大小
 * @returns: 存放数据的位数,不计'\0'
 */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 返回客户端 404消息,url路径找不到 */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* 创建Web服务器的套接字,绑定套接字到指定端口,如果端口为0,说明是动态
 * 分配的端口号,然后获取sockaddr_in,更新端口号,在套接字上进行监听
 * @parameters: 要监听端口的指针
 * @returns: 服务器端套接字 */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    struct sockaddr_in name;

    // 创建套接字
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);

    // 指定INADDR_ANY,查看博客指定INADDR_ANY详解
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    //绑定
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0)  /* 如果是0说明是动态分配的端口号 */
    {
        int namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return(httpd);
}

/**********************************************************************/
/*
 * 处理还没有实现的http方法
 *
 * @parameter: the client socket
 */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/*
 * 入口main函数
 */
/**********************************************************************/
int main(void)
{
    int server_sock = -1;
    u_short port = 0;
    int client_sock = -1;
    struct sockaddr_in client_name;
    int client_name_len = sizeof(client_name);
    pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1)
    {
        client_sock = accept(server_sock,
                             (struct sockaddr *)&client_name,
                             &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        /* 启动一个线程去处理请求accept_request(client_sock); */
        if (pthread_create(&newthread , NULL, accept_request, client_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);

    return(0);
}
