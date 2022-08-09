#define _GNU_SOURCE
#include <stdio.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>

unsigned short COLS;
unsigned short LINES;
unsigned short CLIE_PORT;
unsigned short SERV_PORT;
struct in_addr MIRR_IP;
char* MIRR_ADDR;
int BIND_INTFC;
bool ROUTE_ADDR = 0;
struct termios termsets;

struct in_addr*
gethost(char* addr)
{
  struct hostent* host = gethostbyname(addr);
  if (host == NULL) return NULL;
  struct hostent* reth = gethostbyaddr(host->h_addr_list[0], host->h_length, AF_INET);
  if (reth == NULL) return NULL;
  return (struct in_addr*)reth->h_addr_list[0];
}

void*
client_handler_thr_func(void* clientfd)
{
  int cfd = (int)(long long)clientfd;
  int msock = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in maddr = {
	.sin_family = AF_INET,
	.sin_port = SERV_PORT
  };
  if(ROUTE_ADDR){
	struct in_addr* maddrs = gethost(MIRR_ADDR);
	if(maddrs == NULL){
	  close(cfd);
	  close(msock);
	  return NULL;
	}
	maddr.sin_addr = *maddrs;
  }
  else maddr.sin_addr = MIRR_IP;
  struct pollfd fds[] = {
	{
	  .fd = cfd,
	  .events = POLLIN | POLLRDHUP
	},
	{
	  .fd = msock,
	  .events = POLLIN | POLLRDHUP
	}
  };
  connect(msock, (struct sockaddr*)&maddr, (socklen_t)sizeof(maddr));
  while (1) {
	poll(fds, 2, -1);
	for (int i = 0; i < 2; i++) {
	  if ((fds[i].revents & (POLLRDHUP | POLLHUP | POLLERR)) != 0) {
		close(cfd);
		close(msock);
		return NULL;
	  }
	}
	for (int i = 0; i < 2; i++) {
	  if ((fds[i].revents & POLLIN) == 1) {
		int cap;
		ioctl(fds[i].fd, FIONREAD, &cap);
	    char buf[cap];
		int cnt = read(fds[i].fd, buf, cap);
		write(fds[!i].fd, buf, cnt);
	  }
	}
  }
}

long long
getargopt(const char* arg, const char** param,
		  int arlen, int* idx)
{
  *idx = 0;
  int i = 0;
  char _eq = 0;
  for (; *idx < arlen; (*idx)++) {
	char eq = 0;
	if (param[*idx][strlen(param[*idx])-1] == '=')
	  for (i = 0; i < strlen(param[*idx]); i++) {
		if (arg[i] != param[*idx][i]) {
		  eq = 1;
		}
	  }
	else
	  eq = (strcmp(param[*idx], arg) != 0);
	if (eq == 0) {
	  _eq = 1;
	  break;
	}
	else
	  continue;
  }
  if (_eq)
	return (long long)(i+(char*)arg);
  else
	return -1;
}

void
usage(const char* progname,
	  FILE* stream, int exitcode)
{
  fprintf(stream, "Usage: %s <args>\n",
		  progname);
  fprintf(stream, "Args:\n");
  fprintf(stream, "    port=<num>     Mirroring port\n");
  fprintf(stream, "    Sport=<num>    Clonning server's port\n");
  fprintf(stream, "    Cport=<num>    Itself's server port\n");
  fprintf(stream, "    host=<addr|ip> Mirroring address\n");
  fprintf(stream, "    interface=<ip> Binding interface\n");
  fprintf(stream, "    help           Prints this message\n");
  fprintf(stream, "                   in stdout\n");
  fflush(stream);
  exit(exitcode);
}

void
thread_stop_handler(int sig)
{
  if (sig == SIGUSR1) {
	ioctl(0, TCSETS, &termsets);
	pthread_exit(NULL);
  }
}

void screen_resz_handler(int sig)
{
  if (sig == SIGWINCH) {
	struct winsize win;
	ioctl(0, TIOCGWINSZ, &win);
	LINES = win.ws_row;
	COLS = win.ws_col;
  }
}

struct main_thread_ctx{
  int ssockfd;
  char **argv;
};

void*
main_thr(void* arg)
{
  int ssocket = ((struct main_thread_ctx*)arg) -> ssockfd;
  char** argv = ((struct main_thread_ctx*)arg) -> argv;
  struct sockaddr_in saddr = {
	.sin_family = AF_INET,
	.sin_port = CLIE_PORT,
	.sin_addr = (struct in_addr){.s_addr = BIND_INTFC}
  };
  if (bind(ssocket, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
	fprintf(stderr, "\x1b[s\x1b[G%s: bind failed: %s\x1b[K\n\x1b[u", argv[0], strerror(errno));
	ioctl(0, TCSETS, &termsets);
	putc(10, stdout);
	exit(1);
  }
  if (listen(ssocket, 10) < 0) {
	fprintf(stderr, "\x1b[s\x1b[G%s: listen failed: %s\x1b[K\n\x1b[u", argv[0], strerror(errno));
	ioctl(0, TCSETS, &termsets);
	putc(10, stdout);
	exit(1);
  }
  {
	  int opt = 1;
	  setsockopt(ssocket, SOL_SOCKET, SO_REUSEADDR, &opt, 4);
	  setsockopt(ssocket, SOL_SOCKET, SO_REUSEPORT, &opt, 4);
  }
  while (1) {
	int csock;
	struct sockaddr_in caddr;
	socklen_t caddrl = sizeof(caddr);
	if ((csock = accept(ssocket, (struct sockaddr*)&caddr, &caddrl)) < 0) {
	  fprintf(stderr, "\x1b[s\x1b[G%s: accept failed: %s\x1b[K\n\x1b[u", argv[0], strerror(errno));
	  fflush(stderr);
	  sleep(300);
	  continue;
	}
	printf("\x1b[s\x1b[G%s: accepted connection[fd: %d] from: %s:%d\x1b[K\n\x1b[u", argv[0],
			csock, inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
	printf("\x1b[%d;1H\x1b[7m\x1b[2KControls: stop(C-c)\x1b[00m", LINES);
	fflush(stdout);
	pthread_t cthr;
	pthread_create(&cthr, NULL, client_handler_thr_func, (void*)(long long)csock);
	pthread_detach(cthr);
  }
  return NULL;
}

int
main(int argc, char* argv[])
{
  if (argc < 2)
	usage(argv[0], stderr, 1);
  {
	const char* params[] = {
	  "port=", "host=", "interface=", "help", "Sport=", "Cport="
	};
	for (int i = 1; i < argc; i++) {
	  int idx;
	  char* opt = (char*)getargopt(argv[i], params, sizeof(params)/sizeof(*params), &idx);
	  if ((long long)opt > 0) {
		switch (idx) {
		case 0:
		  CLIE_PORT = htons(atoi(opt));
		  SERV_PORT = htons(atoi(opt));
		  break;
		case 1:
		  if((inet_aton(opt, &MIRR_IP)) == 0){
			ROUTE_ADDR = 1;
			MIRR_ADDR = opt;
		  }
		  break;
		case 2:
		  BIND_INTFC = inet_addr(opt);
		  break;
		case 3:
		  usage(argv[0], stdout, 0);
		  break;
		case 4:
		  SERV_PORT = htons(atoi(opt));
		  break;
		case 5:
		  CLIE_PORT = htons(atoi(opt));
		  break;
		default:
		  fprintf(stderr, "%s: %s not implemented yet\n", argv[0], argv[i]);
		  exit(1);
		}
	  } else {
		fprintf(stderr, "%s: unknown argument %s\n", argv[0], argv[i]);
		exit(1);
	  }
	}
  }
  screen_resz_handler(SIGWINCH);
  if (!ROUTE_ADDR) printf("Mirroring ip: %s\n", inet_ntoa(MIRR_IP));
  else printf("Mirroring addr: %s\n", MIRR_ADDR);
  if (SERV_PORT == CLIE_PORT) printf("Mirroring port: %d\n", ntohs(CLIE_PORT));
  else printf("Ports: User->External Server: %d->%d\n", ntohs(CLIE_PORT), ntohs(SERV_PORT));
  pthread_t main_t;
  int ssocket = socket(AF_INET, SOCK_STREAM, 0);
  struct main_thread_ctx mctx = {
	.ssockfd = ssocket,
	.argv = argv
  };
  pthread_create(&main_t, NULL, main_thr, &mctx);
  ioctl(0, TCGETS, &termsets);
  {
	ioctl(0, TCSETS, &(struct termios){
		.c_iflag = ~(IGNBRK | BRKINT | PARMRK | ISTRIP
					 | IXON),
		.c_lflag = ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN),
		.c_cc[VMIN] = 1
	});
  }
  {
	struct sigaction stopact = {
	  .sa_handler = thread_stop_handler
	};
	sigaction(SIGUSR1, &stopact, NULL);
	struct sigaction winact = {
	  .sa_handler = screen_resz_handler
	};
	sigaction(SIGWINCH, &winact, NULL);
  }
  printf("\x1b[%d;1H\x1b[7m\x1b[2KControls: stop(C-c)\x1b[00m", LINES);
  fflush(stdout);
  int key = 0;
  while (1) {
	read(0, &key, 4);
	if (key == 3) break;
  }
  ioctl(0, TCSETS, &termsets);
  putc(10, stdout);
  pthread_kill(main_t, SIGUSR1);
  close(ssocket);
}
