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

#define EVENT_COUNT 10
char users[1024][64];

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

	while (1) {
		struct epoll_event evs[EVENT_COUNT];

		int res = epoll_wait(efd, evs, EVENT_COUNT, -1);

		for (int i = 0; i < res; i++)
		{
			if ((evs[i].events & EPOLLIN) && evs[i].data.fd == sfd) {
				struct sockaddr in_addr;
				socklen_t in_len = sizeof(in_addr);
				int wfd = accept(sfd, &in_addr, &in_len);
				
				epoll_ctl(efd, EPOLL_CTL_ADD, wfd,
					&(struct epoll_event) {
						.events = EPOLLIN,
						.data.fd = wfd,
					});

				users[wfd][0] = '\0';

				const char *prompt = "Enter your username: ";
				if (write(wfd, prompt, strlen(prompt)) <= 0);

			} else if ((evs[i].events & EPOLLIN)) {
				int wfd = evs[i].data.fd;
				char buf[1024];
				int r = read(wfd, buf, sizeof(buf) - 1);
				
				if (r <= 0) {
					if (users[wfd][0] != '\0') {
						char out[256];
						int len = snprintf(out, sizeof(out), ">>> User %s left the chat\n", users[wfd]);
						
						for (int j = 0; j < 1024; j++) 
							if (users[j][0])
								if (write(j, out, len) <= 0);
					}

					users[wfd][0] = '\0';

					epoll_ctl(efd, EPOLL_CTL_DEL, wfd, NULL);
					close(wfd);

				} else {
					buf[r] = 0;
					for (int j = 0; j < r; j++) if (buf[j] == '\r' || buf[j] == '\n') buf[j] = '\0';

					if (users[wfd][0] == '\0') {
						snprintf(users[wfd], 64, "%s", buf);
						char out[256];
						int len = snprintf(out, sizeof(out), ">>> %s entered the room\n", users[wfd]);
						
						for (int j = 0; j < 1024; j++) 
							if (users[j][0])
							       	if (write(j, out, len) <= 0);

					} else if (strncmp(buf, "\\users", 6) == 0) {
						if (write(wfd, "Online: ", 8) <= 0);
						for (int j = 0; j < 1024; j++) {
							if (users[j][0]) {
								if (write(wfd, users[j], strlen(users[j])) <= 0);
								if (write(wfd, " ", 1) <= 0);
							}
						}
						if (write(wfd, "\n", 1) <= 0);

					} else if (strncmp(buf, "\\quit", 5) == 0) {
						char out[512];
						char *msg = (strlen(buf) > 6) ? buf + 6 : "Goodbye!";
						int len = snprintf(out, sizeof(out), "<<< %s left: %s\n", users[wfd], msg);
						
						for (int j = 0; j < 1024; j++) 
							if (users[j][0])
							       	if (write(j, out, len) <= 0);

						users[wfd][0] = '\0';
												
						epoll_ctl(efd, EPOLL_CTL_DEL, wfd, NULL);
						close(wfd);

					} else {
						if (strlen(buf) > 0) {
							char out[2048];
							int len = snprintf(out, sizeof(out), "[%s]: %s\n", users[wfd], buf);

							for (int j = 0; j < 1024; j++) 
								if (users[j][0]) 
									if (write(j, out, len) <= 0);
						}
					}
				}
			}
		}
	}
}

#if 0
if (users[wfd][0] == '\0') {
    int exists = 0;
    for (int j = 0; j < 1024; j++) {
        if (users[j][0] != '\0' && strcmp(users[j], buf) == 0) {
            exists = 1;
            break;
        }
    }

    if (exists) {
        const char *err = "Error: This name is already taken. Try another: ";
        write(wfd, err, strlen(err));
        
    } else {
        strncpy(users[wfd], buf, 63);
        char out[256];
        int len = snprintf(out, sizeof(out), ">>> %s entered the room\n", users[wfd]);
        
        for (int j = 0; j < 1024; j++) {
            if (users[j][0] != '\0') 
                write(j, out, len);
        }
    }
}
#endif

/* netstat -atn | grep 10001 */
/* nc localhost 10001*/
