#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

static int sfd = -1;
static int con_fd = -1;
static FILE *file = NULL;
static const char *filename = "/var/tmp/aesdsocketdata";
static char ip[INET_ADDRSTRLEN];
static volatile sig_atomic_t stop = 0;

static char * get_ip(struct sockaddr *aconection);
static void signal_handler(int signal);
static void cleanup(void);
static int recv_dyn(int sfd, char **rx_buf);

int main(int argc, char *argv[])
{
    int status;
    bool daemon = false;
    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct sockaddr_storage accepted_conection;

    struct sigaction new_action;

    (void)memset(&hints, 0, sizeof(hints));
    (void)memset(&new_action, 0, sizeof(new_action));

    openlog(__FILE__, LOG_PID, LOG_USER);

    if((argc == 2) && strcmp(argv[1], "-d" ) == 0)
    {
        daemon = true;
    }

    new_action.sa_handler = signal_handler;
    if (sigaction(SIGINT, &new_action, NULL) != 0)
    {
        printf("Error SIGINT");
        return -1;
    }

    if (sigaction(SIGTERM, &new_action, NULL) != 0)
    {
        printf("Error SIGTERM");
        return -1;
    }

    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    if ((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinf: %s \n", gai_strerror(status));
        return -1;
    }

    if ((sfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1)
    {
        fprintf(stderr, "socket: %s \n", strerror(errno));
        freeaddrinfo(servinfo);
        return -1;
    }

    int opt = 1;
    if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        fprintf(stderr, "setsockopt: %s \n", strerror(errno));
        freeaddrinfo(servinfo);
        return -1;
    }

    if ((status = bind(sfd, servinfo->ai_addr, servinfo->ai_addrlen)) != 0)
    {
        fprintf(stderr, "bind: %s \n", strerror(errno));
        freeaddrinfo(servinfo);
        return -1;
    }

    freeaddrinfo(servinfo);

    if(daemon)
    {
        printf("Starting daemon\n");
        pid_t pid = fork();
        if(pid < 0) { perror("fork"); return -1;}
        if(pid !=0) { printf("Parent - cleanup end exit\n"); _exit(0);}

        setsid();
        chdir("/");
    }

    if ((status = listen(sfd, 5)) != 0)
    {
        fprintf(stderr, "listen: %s \n", strerror(errno));
        cleanup();
        return -1;
    }

    file = fopen(filename, "w");
    if (file == NULL)
    {
        perror("Error opening the file");
        syslog(LOG_ERR, "LOG: Can't open the file %s", filename);
        cleanup();
        return 1;
    }
    fclose(file);

    file = fopen(filename, "a+");
    if (file == NULL)
    {
        perror("Error opening the file");
        cleanup();
        return 1;
    }

    while (!stop)
    {
        char *rx_buffer = NULL;
        socklen_t sc_len = sizeof(accepted_conection);
        con_fd = accept(sfd, (struct sockaddr *)&accepted_conection, &sc_len);
        if (con_fd == -1)
        {
            if (errno == EINTR && stop)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            if (stop)
            {
                break;
            }
            fprintf(stderr, "accept: %s\n", strerror(errno));
            cleanup();
            return -1;
        }

        get_ip((struct sockaddr *)&accepted_conection);
        syslog(LOG_INFO, "Accepted connection  from %s", ip);

        int rx_len = recv_dyn(con_fd, &rx_buffer);
        if ((rx_len < 0) || (NULL == rx_buffer))
        {
            free(rx_buffer);
            if (stop)
            {
                break;
            }
            close(con_fd);
            syslog(LOG_INFO, "Closed connection from %s", ip);

            con_fd = -1;
            continue;
        }

        fprintf(file, "%s", rx_buffer);
        fflush(file);
        char *line = NULL;
        size_t len = 0;
        fseek(file, 0, SEEK_SET);
        while ((getline(&line, &len, file)) != -1)
        {
            ssize_t sent = send(con_fd, line, strlen(line), 0);
            if (sent == -1 && errno == EINTR)
            {
                break;
            }
        }
        free(line);
        free(rx_buffer);
        close(con_fd);
        syslog(LOG_INFO, "Closed connection from %s", ip);
        con_fd = -1;
    }
    syslog(LOG_INFO, "Caught signal, exiting");
    cleanup();
    return 0;
}

static int recv_dyn(int sfd, char **rx_buf)
{
    char *buffer = NULL;
    size_t total = 0;

    while (1)
    {
        char temp[1024] = {0};
        ssize_t n = recv(sfd, temp, sizeof(temp), 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                *rx_buf = NULL;
                return -1;
            }
            perror("recv");
            free(buffer);
            *rx_buf = NULL;
            return -1;
        }
        else if (n == 0)
        {
            break;
        }

        char *new_buf = realloc(buffer, total + n + 1);
        if (!new_buf)
        {
            perror("realloc");
            free(buffer);
            *rx_buf = NULL;
            return -1;
        }
        buffer = new_buf;
        memcpy(buffer + total, temp, n);
        total += n;

        if (memchr(temp, '\n', n) != NULL)
        {
            break;
        }
    }

    if (buffer)
        buffer[total] = '\0';
    *rx_buf = buffer;

    return (int)total;
}

static char * get_ip(struct sockaddr *conection)
{
    struct sockaddr_in *addr = (struct sockaddr_in *)conection;

    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    return ip;
}

static void cleanup(void)
{
    if (file)
    {
        fclose(file);
        file = NULL;
    }
    if (con_fd != -1)
    {
        close(con_fd);
        syslog(LOG_INFO, "Closed connection from %s", ip);
        con_fd = -1;
    }
    if (sfd != -1)
    {
        close(sfd);
        sfd = -1;
    }
    unlink(filename);
}

static void signal_handler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        stop = 1;
        if (sfd != -1)
        {
            close(sfd);
            sfd = -1;
        }
        if (con_fd != -1)
        {
            close(con_fd);
            con_fd = -1;
        }
    }
}
