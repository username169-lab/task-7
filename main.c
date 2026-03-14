#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <sys/signalfd.h>

#define EVENT_COUNT 10
#define USER_COUNT 1024
#define NAME_SIZE 64
#define BUF_SIZE 1024
#define PUBLIC 0
#define PRIVATE 1

typedef struct Stats {
    char name[NAME_SIZE];
    int count;
    int pcount;
} stats;

void send_all(char *, int, char **, int);
void send_to(char *, int, int, char *, char **, int);
void who_online(int, char **, int);
void add(int, stats *, int *, int *, char **, int);
void find(stats *, int);

int main(int argc, char *argv[])
{	
        if (argc < 3) {
        	fprintf(stderr, "Error: not enough arguments\n");
		return 1;
        }

	const char *host = argv[1];
	const char *service = argv[2];
	struct addrinfo *addrs = NULL;
	struct addrinfo hints =
	{
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};

	int r = getaddrinfo(host, service, &hints, &addrs);
	if (r != 0) {
		fprintf(stderr, "Error: %s\n", gai_strerror(r));
		return 1;
	}
	
	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (sfd < 0) {
		fprintf(stderr, "Error: socket failed: %s\n", strerror(errno));
		return 1;
	}

	if (bind(sfd, addrs->ai_addr, addrs->ai_addrlen) < 0) {
		fprintf(stderr, "Error: bind failed: %s\n", strerror(errno));
		return 1;
	}

	freeaddrinfo(addrs);

	listen(sfd, 1000);

	int efd = epoll_create1(EPOLL_CLOEXEC);
	epoll_ctl(efd, EPOLL_CTL_ADD, sfd,
		&(struct epoll_event) {
			.events = EPOLLIN,
			.data.fd = sfd,
		});
		
	sigset_t mask;
    	sigemptyset(&mask);
    	sigaddset(&mask, SIGINT);
    	sigprocmask(SIG_BLOCK, &mask, NULL);
    	
    	int sigfd = signalfd(-1, &mask, SFD_NONBLOCK);	
	epoll_ctl(efd, EPOLL_CTL_ADD, sigfd,
		&(struct epoll_event) {
			.events = EPOLLIN,
			.data.fd = sigfd,
		});
	
	char **users = (char **)calloc(USER_COUNT, sizeof(char *));
	int size = USER_COUNT, total = 0;
	stats *history = (stats *)calloc(USER_COUNT, sizeof(stats));
	
	while (1) {
		struct epoll_event evs[EVENT_COUNT];
		int res = epoll_wait(efd, evs, EVENT_COUNT, -1);

		for (int i = 0; i < res; i++)
		{
			if ((evs[i].events & EPOLLIN) && evs[i].data.fd == sfd) {
				struct sockaddr in_addr;
				socklen_t in_len = sizeof(in_addr);
				int wfd = accept(sfd, &in_addr, &in_len);
				if (wfd >= USER_COUNT) {
    					char *busy = "Server is full\n";
    					write(wfd, busy, strlen(busy));
    					close(wfd);
    					continue;
				} else {
					epoll_ctl(efd, EPOLL_CTL_ADD, wfd,
						&(struct epoll_event) {
							.events = EPOLLIN,
							.data.fd = wfd,
						});
				}
				char *prompt = "Enter your username: ";
				if (write(wfd, prompt, strlen(prompt)) <= 0) {}

			} else if ((evs[i].events & EPOLLIN) && evs[i].data.fd == sigfd) {
				struct signalfd_siginfo siginfo;
				while (read(sigfd, &siginfo, sizeof(siginfo)) > 0) {}
				
				find(history, total);
				
				for (int j = 0; j < USER_COUNT; j++)
					if (users[j]) {
						free(users[j]);
						close(j);
					}
				free(users);
				free(history);		
				exit(0);
				
			} else if ((evs[i].events & EPOLLIN)) {
				int wfd = evs[i].data.fd;
				char buf[BUF_SIZE];
				int r = read(wfd, buf, sizeof(buf) - 1);
				
				if (r <= 0) {
					if (users[wfd]) {
						char msg[2*BUF_SIZE];
						int len = snprintf(msg, sizeof(msg), ">>> User %s left the chat\n", users[wfd]);
						send_all(msg, len, users, USER_COUNT);
						
					}
					free(users[wfd]);
					users[wfd] = NULL;

					epoll_ctl(efd, EPOLL_CTL_DEL, wfd, NULL);
					close(wfd);

				} else {
					buf[r] = 0;
					buf[strcspn(buf, "\r\n")] = 0;
					
					if (!users[wfd]) {
					        int is_taken = 0;
                                                for (int j = 0; j < USER_COUNT; j++) {
                                                        if (users[j] && strncmp(users[j], buf, NAME_SIZE - 1) == 0) {
                                                                is_taken = 1;
                                                                break;
                                                        }
                                                }
                                                if (is_taken || strlen(buf) == 0) {
                                                        char *err = "Error: Name taken or empty. Try another: ";
                                                        write(wfd, err, strlen(err));
                                                } else {
						        char *tmp = (char *)calloc(NAME_SIZE, sizeof(char));
						        users[wfd] = tmp;
						        snprintf(users[wfd], NAME_SIZE, "%.*s", NAME_SIZE - 1, buf);
						        char msg[2*BUF_SIZE];
						        int len = snprintf(msg, sizeof(msg), ">>> %s entered the room\n", users[wfd]);
						        send_all(msg, len, users, USER_COUNT);
					        }

					} else if (strncmp(buf, "\\users", 6) == 0) {
						who_online(wfd, users, USER_COUNT);
						
					} else if (strncmp(buf, "\\quit", 5) == 0) {
						char msg[2*BUF_SIZE];
						char *tmp = (strlen(buf) > 6) ? buf + 6 : "Goodbye!";
						int len = snprintf(msg, sizeof(msg), "<<< %s left: %s\n", users[wfd], tmp);
						
						send_all(msg, len, users, USER_COUNT);
						add(wfd, history, &total, &size, users, PUBLIC);
						
						free(users[wfd]);
						users[wfd] = NULL;
						epoll_ctl(efd, EPOLL_CTL_DEL, wfd, NULL);
						close(wfd);

					} else if (strncmp(buf, "\\private ", 9) == 0) {
					        char *target = buf + 9;
                                                char *pmsg = strchr(target, ' ');

                                                if (pmsg) {
                                                        *pmsg = '\0';
                                                        pmsg++;
                
                                                        if (strlen(pmsg) > 0) {
                                                                char msg[2*BUF_SIZE];
                                                                int len = snprintf(msg, sizeof(msg), "[Private from %s]: %s\n", users[wfd], pmsg);
            
                                                                send_to(msg, len, wfd, target, users, USER_COUNT);
                                                        }
                                                        
                                                } else {
                                                        char *err = ">>> Usage: \\private <nickname> <message>\n";
                                                        write(wfd, err, strlen(err));
                                                }
                                                add(wfd, history, &total, &size, users, PRIVATE);
							
					} else {
						if (strlen(buf) > 0) {
							char msg[2*BUF_SIZE];
							int len = snprintf(msg, sizeof(msg), "[%s]: %s\n", users[wfd], buf);
							send_all(msg, len, users, USER_COUNT);
							add(wfd, history, &total, &size, users, PUBLIC);
						}
					}
				}
			}
		}
	}
	
	free(users);
	free(history);
}

void send_all(char *msg, int len, char *users[], int size)
{
	for (int i = 0; i < size; i++) if (users[i]) if (write(i, msg, len) <= 0) {}
}

void send_to(char *msg, int len, int src, char *target, char *users[], int size) {
    for (int i = 0; i < size; i++) {
        if (users[i] && strncmp(users[i], target, NAME_SIZE - 1) == 0) {
            write(i, msg, len);
            return;
        }
    }
    char *err = ">>> User not found\n";
    write(src, err, strlen(err));
}

void who_online(int fd, char *users[], int size)
{
	if (write(fd, "Online: ", 8) <= 0) {}
	for (int i = 0; i < size; i++) {
		if (users[i]) {
			if (write(fd, users[i], strlen(users[i])) <= 0) {}
			if (write(fd, " ", 1) <= 0) {}
		}
	}
	if (write(fd, "\n", 1) <= 0) {}
}

void add(int fd, stats *history, int *ptotal, int *psize, char *users[], int flag)
{
	int found = 0;
	for (int i = 0; i < *ptotal; i++) {
                /* calloc feature, users[fd] undefined ? */
    		if (strncmp(history[i].name, users[fd], sizeof(history[i].name)) == 0) {
        		if (flag == PRIVATE) history[i].pcount++;
                        else history[i].count++;
        		found = 1;
        		break;
    		}
	}

	if (!found && *ptotal < *psize) {
	    	snprintf(history[*ptotal].name, sizeof(history[*ptotal].name), "%s", users[fd]);
	    	if (flag == PRIVATE) {
	    	        history[*ptotal].pcount = 1;
	    	        history[*ptotal].count = 0;
	    	} else {
	    		history[*ptotal].pcount = 0;
	    	        history[*ptotal].count = 1;
	    	}
    		(*ptotal)++;
	}	
}

void find(stats *history, int total)
{
	int max = -1, imax = -1;
	int pmax = -1, pimax = -1;
	for (int i = 0; i < total; i++) {
		if (history[i].count > max) {
			max = history[i].count;
			imax = i;
		}
		if (history[i].pcount > pmax) {
			pmax = history[i].pcount;
			pimax = i;
		}
	}
					
	if (imax != -1) printf("Most replies by %s. Count: %d\n", history[imax].name, history[imax].count);
	if (pimax != -1) printf("Most privates by %s. Count: %d\n", history[pimax].name, history[pimax].pcount);
}				

/* netstat -atn | grep 10001 */
/* nc localhost 10001*/
