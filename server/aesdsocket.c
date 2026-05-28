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

#include <pthread.h>

#include <sys/queue.h>
#include <time.h>
#include <locale.h>

struct thread_data {
    _Atomic bool thread_completed;
    int connection_fd;
    pthread_t pid;
    pthread_mutex_t *pmutex;
    char ip[INET_ADDRSTRLEN];
    SLIST_ENTRY(thread_data) next;
};

SLIST_HEAD(connection_list, thread_data);

static pthread_mutex_t mutex;
static int sfd = -1;
static FILE *file = NULL;
static const char *filename = "/var/tmp/aesdsocketdata";
static volatile sig_atomic_t stop = 0;

static  void get_ip(struct sockaddr *connection , char *ip, socklen_t out_len);
static void signal_handler(int signal);
static void cleanup(void);
static int recv_dyn(int sfd, char **rx_buf);

void *thread_funct(void *thread_param);
void *timer_thread(void *thread_param);

int main(int argc, char *argv[])
{
    int status;
    bool daemon = false;
    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct sockaddr_storage accepted_conection;
    struct sigaction new_action;

    struct connection_list head;
    SLIST_INIT(&head);
  
    memset(&hints, 0, sizeof(hints));
    memset(&new_action, 0, sizeof(new_action));

    openlog(__FILE__, LOG_PID, LOG_USER);

    pthread_mutex_init(&mutex, NULL);

    if ((argc == 2) && strcmp(argv[1], "-d") == 0) {
        daemon = true;
    }

    new_action.sa_handler = signal_handler;
    if (sigaction(SIGINT, &new_action, NULL) != 0) {
        printf("Error SIGINT");
        return -1;
    }

    if (sigaction(SIGTERM, &new_action, NULL) != 0) {
        printf("Error SIGTERM");
        return -1;
    }

    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    if ((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    if ((sfd = socket(servinfo->ai_family, servinfo->ai_socktype,
                       servinfo->ai_protocol)) == -1) {
        fprintf(stderr, "socket: %s\n", strerror(errno));
        freeaddrinfo(servinfo);
        return -1;
    }

    int opt = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        fprintf(stderr, "setsockopt: %s\n", strerror(errno));
        freeaddrinfo(servinfo);
        return -1;
    }

    if ((status = bind(sfd, servinfo->ai_addr, servinfo->ai_addrlen)) != 0) {
        fprintf(stderr, "bind: %s\n", strerror(errno));
        freeaddrinfo(servinfo);
        return -1;
    }

    freeaddrinfo(servinfo);

    if (daemon) {
        printf("Starting daemon\n");
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return -1;
        }
        if (pid != 0) {
            printf("Parent - cleanup end exit\n");
            _exit(0);
        }

        setsid();
        chdir("/");
        
    }

    if ((status = listen(sfd, 5)) != 0) {
        fprintf(stderr, "listen: %s\n", strerror(errno));
        cleanup();
        return -1;
    }

    file = fopen(filename, "w");
    if (file == NULL) {
        perror("Error opening the file");
        syslog(LOG_ERR, "LOG: Can't open the file %s", filename);
        cleanup();
        return 1;
    }
    fclose(file);

    file = fopen(filename, "a+");
    if (file == NULL) {
        perror("Error opening the file");
        cleanup();
        return 1;
    }
    pthread_t t_tid;
    pthread_create(&t_tid, NULL, timer_thread, &mutex);

    while (!stop) {
        socklen_t sc_len = sizeof(accepted_conection);
        struct thread_data *td;

        int con_fd = accept(sfd, (struct sockaddr *)&accepted_conection, &sc_len);
        if (con_fd == -1) {
            if (errno == EINTR) {
                if (stop) {
                    break;
                }
                continue;
            }
            if (stop) {
                break;
            }
            fprintf(stderr, "accept: %s\n", strerror(errno));
            cleanup();
            return -1;
        }

        td = malloc(sizeof(struct thread_data));
        if (td == NULL) {
            perror("malloc");
            continue;
        }
        SLIST_INSERT_HEAD(&head, td, next);

        get_ip((struct sockaddr *)&accepted_conection, td->ip, INET_ADDRSTRLEN);
        td->connection_fd = con_fd;
        td->pmutex = &mutex;
        td->thread_completed = false;
        syslog(LOG_INFO, "Accepted connection from %s", td->ip);

        pthread_create(&td->pid, NULL, thread_funct, td);

        struct thread_data *tmp, *nxt;

        tmp = SLIST_FIRST(&head);
        while (tmp != NULL) {
            nxt = SLIST_NEXT(tmp, next);
            if(tmp->thread_completed) {
                pthread_join(tmp->pid, NULL);
                SLIST_REMOVE(&head, tmp, thread_data, next);
                free(tmp);
            }
            tmp = nxt;
        }

    }

    struct thread_data *tmp, *nxt;
        tmp = SLIST_FIRST(&head);
        while (tmp != NULL) {
            nxt = SLIST_NEXT(tmp, next);
            pthread_join(tmp->pid, NULL);
            SLIST_REMOVE(&head, tmp, thread_data, next);
            free(tmp);
            tmp = nxt;
        }

    pthread_join(t_tid, NULL);
    syslog(LOG_INFO, "Caught signal, exiting");
    cleanup();
    pthread_mutex_destroy(&mutex);
    return 0;
}

static int recv_dyn(int sfd, char **rx_buf)
{
    char *buffer = NULL;
    size_t total = 0;

    while (1) {
        char temp[1024] = {0};
        ssize_t n = recv(sfd, temp, sizeof(temp), 0);
        if (n < 0) {
            if (errno == EINTR) {
                *rx_buf = NULL;
                return -1;
            }
            perror("recv");
            free(buffer);
            *rx_buf = NULL;
            return -1;
        } else if (n == 0) {
            break;
        }

        char *new_buf = realloc(buffer, total + n + 1);
        if (!new_buf) {
            perror("realloc");
            free(buffer);
            *rx_buf = NULL;
            return -1;
        }
        buffer = new_buf;
        memcpy(buffer + total, temp, n);
        total += n;

        if (memchr(temp, '\n', n) != NULL) {
            break;
        }
    }

    if (buffer) {
        buffer[total] = '\0';
    }
    *rx_buf = buffer;

    return (int)total;
}

static  void get_ip(struct sockaddr *connection , char *ip, socklen_t out_len)
{
    struct sockaddr_in *addr = (struct sockaddr_in *)connection;
    inet_ntop(AF_INET, &addr->sin_addr, ip, out_len);
}

static void cleanup(void)
{
    if (file) {
        fclose(file);
        file = NULL;
    }
    if (sfd != -1) {
        close(sfd);
        sfd = -1;
    }
    unlink(filename);
}

static void signal_handler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) {
        stop = 1;
        if (sfd != -1) {
            close(sfd);
            sfd = -1;
        }
    }
}

void * thread_funct(void *thread_param)
{
    struct thread_data *a = (struct thread_data *)thread_param;

    if (a == NULL) {
        perror("Invalid thread_param");
        return NULL;
    }
    
    char *rx_buffer = NULL;

    int rx_len = recv_dyn(a->connection_fd, &rx_buffer);
    if ((rx_len < 0) || (NULL == rx_buffer)) {
        free(rx_buffer);
        close(a->connection_fd);
        syslog(LOG_INFO, "Closed connection from %s", a->ip);
        a->thread_completed = true;
        a->connection_fd = -1;
        return NULL;
    }

    pthread_mutex_lock(a->pmutex);

    fprintf(file, "%s", rx_buffer);
    fflush(file);

    char *line = NULL;
    size_t len = 0;
    fseek(file, 0, SEEK_SET);
    while ((getline(&line, &len, file)) != -1) {
        ssize_t sent = send(a->connection_fd, line, strlen(line), 0);
        if (sent == -1) {
            break;
        }
    }
    free(line);
    pthread_mutex_unlock(a->pmutex);

    free(rx_buffer);
    close(a->connection_fd);
    syslog(LOG_INFO, "Closed connection from %s", a->ip);
    a->thread_completed = true;
    return NULL;
}

void *timer_thread(void *thread_param)
{
    (void)thread_param;
    time_t now;
    struct tm tm_now;
    char cur_time[64];

    setlocale(LC_TIME, "C");

    while(!stop)
    {
        for (int i = 0; i < 100 && !stop; i++) {
            usleep(100000);
        }
        now = time(NULL);
        localtime_r(&now, &tm_now);
        strftime(cur_time, sizeof(cur_time), "timestamp:%a, %d %b %Y %T %z\n", &tm_now);
        pthread_mutex_lock(&mutex);
        printf("%s", cur_time);
        fprintf(file, "%s", cur_time);
        fflush(file);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}