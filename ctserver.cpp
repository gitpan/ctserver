/*---------------------------------------------------------------------------*\

    FILE....: CTSERVER.CPP
    TYPE....: C++ program
    AUTHOR..: David Rowe
    DATE....: 10/10/01

    Computer Telephony (CT) server.  Executes cc-script like commands on CT
    hardware on behalf of client programs.
 	 
\*---------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------*\

       ctserver - client/server library for Computer Telephony programming 

       Copyright (C) 2001 David Rowe david@voicetronix.com.au

       This library is free software; you can redistribute it and/or
       modify it under the terms of the GNU Lesser General Public 
       License as published by the Free Software Foundation; either
       version 2.1 of the License, or (at your option) any later version.

       This library is distributed in the hope that it will be useful,
       but WITHOUT ANY WARRANTY; without even the implied warranty of
       MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
       Lesser General Public License for more details.

       You should have received a copy of the GNU Lesser General Public
       License along with this library; if not, write to the Free Software
       Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
       USA.

\*--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------*\

				   INCLUDES

\*--------------------------------------------------------------------------*/

#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <vpbapi.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <syslog.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#define SUCCESS            0
#define ERROR              1  

#define END_LINE           0x0A
#define SERVER_PORT        1200          
#define MAX_MSG            100

#define NUM_PORTS          4
#define SEC2MS             1000
#define N                  160

// state machine states
#define PLAYING            0
#define WAIT_FOR_PLAYEND   1
#define FINISHED           2
#define RECORDING          3
#define WAIT_FOR_RECORDEND 1

// size of buffer to store CID signal in (may need to be adjusted)
#define CIDN (8000*4)

typedef struct {
	int   h;
	short buf[CIDN];
} THREAD_INFO;
	
/*--------------------------------------------------------------------------*\

			  FUNCTION PROTOTYPES

\*--------------------------------------------------------------------------*/

void *port_thread(void *pv);
int read_line(int newSd, char *line_to_return);
static int digit_match(char digit, char *term_digits);
void sig_handler(int sig);
int ctwaitforevent(int h, void *timer,int event_type,int data,int timeout_ms);
void ctwaitforring(int h, void *timer, int newSd);
void ctwaitfordial(int h, void *timer, int newSd);
void ctplay(int h, int newSd);
void ctrecord(int h, int newSd);
void ctsleep(int h, int newSd, void *timer);
void ctcollect(int h, int newSd);
void ctdial(int h, int newSd);
void mylog(int messtype, char *fmt, ...);
void trim(char *audio_file, int lose);
void wave_get_size(void *wv, unsigned long *bytes);
int arg_exists(int argc, char *argv[], char *arg);
void *rec_thread(void *pv);

/*--------------------------------------------------------------------------*\

				GLOBALS

\*--------------------------------------------------------------------------*/

pthread_mutex_t mutex;          // mutex to protect shared data
int             finito;         // flag to signal the program is finished.
int             threads_active; // the number of active threads.
sigjmp_buf      jmpbuf;
int             syslog_enabled; //  to log messags to console, 1 to syslog
  
/*--------------------------------------------------------------------------*\

				MAIN

\*--------------------------------------------------------------------------*/

int main (int argc, char *argv[]) {
  int       i;
  int       h[NUM_PORTS];
  pthread_t aport_thread[NUM_PORTS];

  openlog(argv[0], LOG_PID, LOG_DAEMON);
  pthread_mutex_init(&mutex,NULL);
  vpb_seterrormode(VPB_ERROR_CODE);
  threads_active = 0;
  finito = 0;

  if (arg_exists(argc,argv,"-h") || arg_exists(argc,argv,"--help")) {
	  printf("usage: %s [-h --help -d -nv]\n", argv[0]);
	  printf("-d             run as a daemon\n");
	  printf("-h or --help   print this message\n");
	  printf("-nv            non-verbose mode (daemon only)\n");
	  exit(0);
  }

  if (arg_exists(argc,argv,"-d")) {
	  // OK - lets turn into a daemon

	  pid_t pid;

	  if ((pid = fork()) < 0) {
		  syslog(LOG_ERR, "Couldnt fork()!");
		  exit(-1);
	  }
	  if (pid != 0) {
		  // save pid to help shutdown
		  FILE *fpid = fopen("/var/run/ctserver.pid","wt");
		  assert(fpid != NULL);
		  fprintf(fpid,"%d",pid);
		  fclose(fpid);

		  // parent process - bye bye
		  if (!arg_exists(argc,argv,"-nv")) {
			  printf("My pid is %d\n",pid);
			  printf("Type 'kill <pid>' to finish\n");
			  printf("Type 'tail /var/log/messages' to monitor " 
				 "debug info\n");
		  }
		  exit(0);
	  }

	  // child process - lets daemonise

	  setsid();
	  chdir("/");
	  umask(0);
	  close(STDIN_FILENO); close(STDOUT_FILENO); close(STDERR_FILENO);

	  syslog_enabled = 1;
	  openlog("ctserver", 0, LOG_DAEMON);
  }
  else
      syslog_enabled = 0;

  // open VPB & TCP/IP ports and start a thread for each port 
  for(i=0; i<NUM_PORTS; i++) {
    h[i] = vpb_open(1,i+1);
    pthread_create(&aport_thread[i], NULL, port_thread, (void*)&h[i]);
  }

  // set up SIGTERM handler to allow for an orderly exit
  signal(SIGTERM, sig_handler);
  int term_sig = sigsetjmp(jmpbuf, 0);

  // program will jump here with term_sig == 1 when SIGTERM occurs
  if (term_sig) {
    // shut down code...........
    mylog(LOG_INFO, "shutting down.... ");

    // Note: we ignore any running threads, which could lead to problems

    // shut down and clean up
    for(i=0; i<NUM_PORTS; i++) {
      vpb_close(h[i]);
    }

    unlink("/var/run/ctsrver.pid");
    mylog(LOG_INFO, "shut down OK!");
    return 0; 
  }

  mylog(LOG_INFO, "Started!");

  // do nothing in main thread.......until SIGTERM occurs
  while(1)
    vpb_sleep(1000);
}

// - server thread, one of these threads is started for each CT port
// - each CT port has a different IP port.

void *port_thread(void *pv) {
  int                sd, newSd;
  socklen_t          cliLen;
  struct sockaddr_in cliAddr, servAddr;
  char               line[MAX_MSG];

  int                h;
  void               *timer;
  char               s[VPB_MAX_STR];
  int                rc;

  pthread_mutex_lock(&mutex);
  threads_active++;
  pthread_mutex_unlock(&mutex);

  h = *(int*)pv;
  vpb_sethook_sync(h,VPB_ONHOOK);
  vpb_timer_open(&timer, h, 0, 1000);

  /* create socket */
  sd = socket(AF_INET, SOCK_STREAM, 0);
  if(sd<0) {
    mylog(LOG_ERR,"[%02d] cannot create socket %s", h, strerror(errno));
    return NULL;
  }
  
  /* bind server port */
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servAddr.sin_port = htons(SERVER_PORT+h);
  
  if(bind(sd, (struct sockaddr *) &servAddr, sizeof(servAddr))<0) {
    mylog(LOG_ERR,"[%02d] cannot bind port %d: %s", h, SERVER_PORT+h,
	  strerror(errno));
    return NULL;
  }

  listen(sd,5);
  
  while(!finito) {

    mylog(LOG_INFO,"[%02d] waiting for data on port TCP %u",h,SERVER_PORT+h);

    cliLen = sizeof(cliAddr);
    newSd = accept(sd, (struct sockaddr *) &cliAddr, &cliLen);
    if(newSd<0) {
      mylog(LOG_ERR,"[%02d] cannot accept connection %s", h, strerror(errno));
      return NULL;
    }
        
    // wait for command

    memset(line,0x0,MAX_MSG);
    while(read_line(newSd,line)!=ERROR) {
      
      strncpy(s, line, strlen(line)-1);
      s[strlen(line)-1] = 0;
      mylog(LOG_INFO,"[%02d] received from %s:TCP%d : %s", h,
	     inet_ntoa(cliAddr.sin_addr),
	     ntohs(cliAddr.sin_port), s);
      
      if (strcmp(line,"ctwaitforring\n")==0) {
	ctwaitforring(h, timer, newSd);
      }
      if (strcmp(line,"ctwaitfordial\n")==0) {
	ctwaitfordial(h, timer, newSd);
      }
      if (strcmp(line,"cthangup\n")==0) {
	      vpb_sethook_sync(h,VPB_ONHOOK);
	      sprintf(s, "OK\n");
	      rc = send(newSd, s, strlen(s)+1, 0);
      }
      if (strcmp(line,"ctanswer\n")==0) {
	      vpb_sethook_sync(h,VPB_OFFHOOK);
	      sprintf(s, "OK\n");
	      rc = send(newSd, s, strlen(s)+1, 0);
      }
      if (strcmp(line,"ctplay\n")==0) {
	ctplay(h, newSd);
      }
      if (strcmp(line,"ctrecord\n")==0) {
	ctrecord(h, newSd);
      }
      if (strcmp(line,"ctsleep\n")==0) {
	ctsleep(h, newSd, timer);
      }
      if (strcmp(line,"ctclear\n")==0) {
	vpb_flush_digits(h);
	sprintf(s, "OK\n");
	rc = send(newSd, s, strlen(s)+1, 0);
      }
      if (strcmp(line,"ctcollect\n")==0) {
	ctcollect(h, newSd);
      }
      if (strcmp(line,"ctdial\n")==0) {
	ctdial(h, newSd);
      }
    
      memset(line,0x0,MAX_MSG);
    } /* while(read_line) */

    mylog(LOG_INFO,"[%02d] connection closed!\n",h);

  } /* while (!finito) */

  pthread_mutex_lock(&mutex);
  threads_active--;
  pthread_mutex_unlock(&mutex);

  return NULL;
}

/* rcv_line is my function readline(). Data is read from the socket when */
/* needed, but not byte after bytes. All the received data is read.      */
/* This means only one call to recv(), instead of one call for           */
/* each received byte.                                                   */
/* You can set END_CHAR to whatever means endofline for you. (0x0A is \n)*/
/* read_lin returns the number of bytes returned in line_to_return       */

int read_line(int newSd, char *line_to_return) {
  
  static int rcv_ptr=0;
  static char rcv_msg[MAX_MSG];
  static int n;
  int offset;

  offset=0;

  while(1) {
    if(rcv_ptr==0) {
      /* read data from socket */
      memset(rcv_msg,0x0,MAX_MSG); /* init buffer */
      n = recv(newSd, rcv_msg, MAX_MSG, 0); /* wait for data */
      if (n<0) {
	perror(" cannot receive data ");
	return ERROR;
      } else if (n==0) {
	close(newSd);
	return ERROR;
      }
    }
  
    /* if new data read on socket */
    /* OR */
    /* if another line is still in buffer */

    /* copy line into 'line_to_return' */
    while(*(rcv_msg+rcv_ptr)!=END_LINE && rcv_ptr<n) {
      memcpy(line_to_return+offset,rcv_msg+rcv_ptr,1);
      offset++;
      rcv_ptr++;
    }
    
    /* end of line + end of buffer => return line */
    if(rcv_ptr==n-1) { 
      /* set last byte to END_LINE */
      *(line_to_return+offset)=END_LINE;
      rcv_ptr=0;
      return ++offset;
    } 
    
    /* end of line but still some data in buffer => return line */
    if(rcv_ptr <n-1) {
      /* set last byte to END_LINE */
      *(line_to_return+offset)=END_LINE;
      rcv_ptr++;
      return ++offset;
    }

    /* end of buffer but line is not ended => */
    /*  wait for more data to arrive on socket */
    if(rcv_ptr == n) {
      rcv_ptr = 0;
    } 
    
  } /* while */
}
  
/*---------------------------------------------------------------------------*\

	FUNCTION: digit_match
	AUTHOR..: David Rowe
	DATE....: 29/7/98

	Determines if the digit is a member of the term_digit string.

\*--------------------------------------------------------------------------*/

static int digit_match(char digit, char *term_digits)
{
	char *p = term_digits;

	while(*p != 0) {
		if (*p == digit)
			return(1);
		p++;
	}

	return(0);
}

/*--------------------------------------------------------------------------*\

	FUNCTION....: sig_handler
	AUTHOR......: David Rowe
	DATE CREATED: 13/09/01

	SIGTERM handler.

\*--------------------------------------------------------------------------*/

void sig_handler(int sig) {
  if (sig == SIGTERM) {
    signal(SIGTERM,SIG_DFL);
    siglongjmp(jmpbuf, 0);
  }
}

/*--------------------------------------------------------------------------*\

	FUNCTION....: ctwaitforring
	AUTHOR......: David Rowe
	DATE CREATED: 10/10/01

	Waits for two rings, decodes CID if present.

\*--------------------------------------------------------------------------*/

void ctwaitforring(int h, void *timer, int newSd) {
  char        s[VPB_MAX_STR], cid_str[VPB_MAX_STR];
  int         state, ret, rc, ev;
  THREAD_INFO ti;
  pthread_t   thread;

  state = 0;
  do {
    // wait forever for first ring
    ctwaitforevent(h, timer, VPB_RING, 0, 0);
    ti.h = h;
    pthread_create(&thread, NULL, rec_thread, (void*)&ti);
    mylog(LOG_INFO,"[%02d] First Ring-CID recording-waiting for second ring",
	  h);
	
    // wait for 6 seconds for second ring, otherwise time out
    ev = ctwaitforevent(h, timer, VPB_RING, 0, 6000);
    vpb_record_terminate(h);
    if (ev == VPB_RING) {
      mylog(LOG_INFO,"[%02d] Second Ring, CID decoding", h);
      ret = vpb_cid_decode(cid_str, ti.buf, N);
      mylog(LOG_INFO,"[%02d] CID decoding ret = %d, number = %s",
	    h, ret, cid_str);
      sprintf(s, "%s\n", cid_str);
      state = 1;
    }
  } while(!finito && !state);

  if (finito) {
    sprintf(s, "finito\n");
  }

  rc = send(newSd, s, strlen(s)+1, 0);
}

/*--------------------------------------------------------------------------*\

	FUNCTION....: ctwaitfordial
	AUTHOR......: David Rowe
	DATE CREATED: 10/10/01

	Waits for dial tone.

\*--------------------------------------------------------------------------*/

void ctwaitfordial(int h, void *timer, int newSd) {
  int         rc, ev;

  ev = ctwaitforevent(h, timer, VPB_TONEDETECT, VPB_DIAL, 0);
  rc = send(newSd,"\n", strlen("\n")+1, 0);
}

/*--------------------------------------------------------------------------*\

	FUNCTION....: ctplay
	AUTHOR......: David Rowe
	DATE CREATED: 10/10/01

	Play handler.

\*--------------------------------------------------------------------------*/

void ctplay(int h, int newSd) {
  char      s[VPB_MAX_STR];
  char      smess[VPB_MAX_STR];
  int       state, next_state, ret, rc;
  VPB_EVENT e;
  char      line[MAX_MSG];
  char      *ext;

  // read file name
  memset(line,0x0,MAX_MSG);
  read_line(newSd,line);
  line[strlen(line)-1] = 0;
  sprintf(s, "%s",line);

  ext = strrchr(s, '.');
  if (!strcmp(ext,".ul"))
      ret = vpb_play_voxfile_async(h, s, VPB_MULAW, 0);
  else
      ret = vpb_play_file_async(h, s, 0);
      
  if (ret != VPB_OK) {
	  rc = send(newSd, "ERROR\n", strlen("ERROR\n")+1, 0);
	  mylog(LOG_ERR,"Error playing: %s", s);
	  return;
  }  

  state = PLAYING;

  do {
    ret = vpb_get_event_ch_async(h, &e);

    if (ret == VPB_OK) {
      vpb_translate_event(&e, s); s[strlen(s)-1]=0;
      mylog(LOG_INFO,"%s",s);

      next_state = state;
      switch(state) {
      case PLAYING:
	      
	if (e.type == VPB_PLAYEND) {
	  next_state = FINISHED;
	  rc = send(newSd, "OK\n", strlen("OK\n")+1, 0);
	}
	    
	if (e.type == VPB_DTMF) {
	  next_state = WAIT_FOR_PLAYEND;
	  vpb_play_terminate(h);
	  sprintf(smess, "%c\n", e.data);
	}
	break;

      case WAIT_FOR_PLAYEND:
	if (e.type == VPB_PLAYEND) {
	  next_state = FINISHED;
	  rc = send(newSd, smess, strlen(smess)+1, 0);
	}
	break;
      }
      state = next_state;
    }
    else
      vpb_sleep(100);
  } while(state != FINISHED);
	
}

/*--------------------------------------------------------------------------*\

	FUNCTION....: ctrecord
	AUTHOR......: David Rowe
	DATE CREATED: 10/10/01

	Record handler.

\*--------------------------------------------------------------------------*/

void ctrecord(int h, int newSd) {
  char      file_name[VPB_MAX_STR], s[VPB_MAX_STR];
  char      smess[VPB_MAX_STR];
  int       state, next_state, ret, rc;
  char      line[MAX_MSG];
  VPB_EVENT e;

  // filename
  memset(line,0x0,MAX_MSG);
  read_line(newSd,line);
  line[strlen(line)-1] = 0;
  sprintf(file_name, "%s",line);

  // timeout
  memset(line,0x0,MAX_MSG);
  read_line(newSd,line);
  unsigned int timeout = atoi(line)*SEC2MS;
  VPB_RECORD r = {"", timeout};
  vpb_record_set(h, &r);
	
  // term digits
  char term_digits[VPB_MAX_STR];
  memset(line,0x0,MAX_MSG);
  read_line(newSd,term_digits);

  ret = vpb_record_file_async(h, file_name, VPB_MULAW);
  if (ret != VPB_OK) {
	  rc = send(newSd, "ERROR\n", strlen("ERROR\n")+1, 0);
	  mylog(LOG_ERR,"Error recording: %s", file_name);
	  return;
  }  

  state = RECORDING;

  do {
    ret = vpb_get_event_ch_async(h, &e);
    if (ret == VPB_OK) {
      vpb_translate_event(&e, s); s[strlen(s)-1]=0;
      mylog(LOG_INFO,"%s",s);

      next_state = state;
      switch(state) {
      case RECORDING:
	      
	if (e.type == VPB_RECORDEND) {
	  next_state = FINISHED;
	  sprintf(smess, "OK\n");
	}
	    
	if (e.type == VPB_DTMF) {
	  if (digit_match(e.data, term_digits)) {
	    next_state = WAIT_FOR_RECORDEND;
	    vpb_record_terminate(h);
	    sprintf(smess, "OK\n");
	  }
	}
	break;

      case WAIT_FOR_RECORDEND:
	if (e.type == VPB_RECORDEND) {
	  next_state = FINISHED;
	}
	break;
      }
      state = next_state;
    }
    else
      vpb_sleep(100);
  } while(state != FINISHED);	

  trim(file_name, 2000);
  rc = send(newSd, smess, strlen(smess)+1, 0);
}

/*--------------------------------------------------------------------------*\

	FUNCTION....: ctsleep
	AUTHOR......: David Rowe
	DATE CREATED: 10/10/01

	Sleep handler.

\*--------------------------------------------------------------------------*/

void ctsleep(int h, int newSd, void *timer) {
  char      s[VPB_MAX_STR];
  int       state, ret, rc;
  char      line[MAX_MSG];
  VPB_EVENT e;

  // read duration of sleep
  memset(line,0x0,MAX_MSG);
  read_line(newSd,line);
  unsigned long newperiod;
  newperiod = atol(line)*SEC2MS;

  vpb_timer_change_period(timer, newperiod);	
  vpb_timer_start(timer);

  state = 0;
  do {
    ret = vpb_get_event_ch_async(h, &e);
    if (ret == VPB_OK) {
      vpb_translate_event(&e, s); s[strlen(s)-1]=0;
      mylog(LOG_INFO,"%s",s); 
      if (e.type == VPB_TIMEREXP) {
	state = 1;
	rc = send(newSd, "OK\n", strlen("OK\n")+1, 0);
      }
      if (e.type == VPB_DTMF) {
	state = 1;
	vpb_timer_stop(timer);
	sprintf(s, "%c\n", e.data);
	rc = send(newSd, s, strlen(s)+1, 0);
      }
    }
    else
      vpb_sleep(100);
  } while(!state);
}

/*--------------------------------------------------------------------------*\

	FUNCTION....: ctcollect
	AUTHOR......: David Rowe
	DATE CREATED: 10/10/01

	Collect digits handler.

\*--------------------------------------------------------------------------*/

void ctcollect(int h, int newSd) {
  char      s[VPB_MAX_STR];
  int       state, ret, rc;
  char      line[MAX_MSG];
  VPB_EVENT e;
  int       digits;
  char      buf[VPB_MAX_STR];

  // read digits
  memset(line,0x0,MAX_MSG);
  read_line(newSd,line);
  digits = atoi(line);

  // read time out in seconds
  memset(line,0x0,MAX_MSG);
  read_line(newSd,line);
  int unsigned long seconds = atol(line);

  // read inter digit time out in seconds
  memset(line,0x0,MAX_MSG);
  read_line(newSd,line);
  int unsigned long inter_seconds = atol(line);

  VPB_DIGITS d = {"", digits, seconds*SEC2MS, inter_seconds*SEC2MS};
  vpb_get_digits_async(h, &d, buf);
	
  state = 0;
  do {
    ret = vpb_get_event_ch_async(h, &e);
    if (ret == VPB_OK) {
      vpb_translate_event(&e, s); s[strlen(s)-1]=0;
      mylog(LOG_INFO,"%s",s);

      if (e.type == VPB_DIGIT) {
	state = 1;
	sprintf(s, "%s\n", buf);
	rc = send(newSd, s, strlen(s)+1, 0);
      }
    }
    else
      vpb_sleep(100);
  } while(!state);
	
}

/*--------------------------------------------------------------------------*\

	FUNCTION....: ctdial
	AUTHOR......: David Rowe
	DATE CREATED: 10/10/01

	Dial handler.

\*--------------------------------------------------------------------------*/

void ctdial(int h, int newSd) {
  char      s[VPB_MAX_STR];
  int       state, ret, rc;
  char      line[MAX_MSG];
  VPB_EVENT e;

  // read dial string
  memset(line,0x0,MAX_MSG);
  read_line(newSd,line);
  line[strlen(line)-1]=0;
  printf("dial: %s %d\n",line,strlen(line));
  
  ret = vpb_dial_async(h, line);
  if (ret != VPB_OK) {
	  rc = send(newSd, "ERROR\n", strlen("ERROR\n")+1, 0);
	  mylog(LOG_ERR,"Error recording");
	  return;
  }  
	
  state = 0;
  do {
    ret = vpb_get_event_ch_async(h, &e);
    if (ret == VPB_OK) {
      vpb_translate_event(&e, s); s[strlen(s)-1]=0;
      mylog(LOG_INFO,"%s",s);

      if (e.type == VPB_DIALEND) {
	state = 1;
	sprintf(s, "OK\n");
	rc = send(newSd, s, strlen(s)+1, 0);
      }
    }
    else
      vpb_sleep(100);
  } while(!state);
	
}

/*--------------------------------------------------------------------------*\

	FUNCTION....: mylog
	AUTHOR......: David Rowe
	DATE CREATED: 10/10/01

	Allows switching between syslog and console output of messages.

\*--------------------------------------------------------------------------*/

void mylog(int messtype, char *fmt, ...) {
  char    s[VPB_MAX_STR];
  va_list argptr;

  va_start(argptr, fmt);
  vsprintf(s, fmt, argptr);
  va_end(argptr);
  
  if(syslog_enabled) {
    syslog(messtype, s);
  }
  else {
    printf("%s\n",s);
  }

}

/*--------------------------------------------------------------------------*\

	FUNCTION....: trim
	AUTHOR......: David Rowe
	DATE CREATED: 10/10/01

	Removes the last 'lose' samples from an audio file.

\*--------------------------------------------------------------------------*/

void trim(char *audio_file, int lose) {
	void               *wr,*rd;
	char               buf[N];
	short unsigned int mode;
	long unsigned int  size,i;
	char               tmp_file[VPB_MAX_STR], tmp_ext[VPB_MAX_STR], *p;
	int                ret;

	// generate tmp file name
	strcpy(tmp_file, audio_file);
	sprintf(tmp_ext, ".%d", rand());
	p = strchr(tmp_file, '.');
	if (p == NULL) {
		// no ext on audio_file, add new one
		strcat(tmp_file, tmp_ext);
	}
	else {
		// replace existing ext
		*p = 0;
		strcat(tmp_file, tmp_ext);
	}

	ret = vpb_wave_open_read(&rd, audio_file);
	if (ret < 0) {
		mylog(LOG_INFO,"trim: error opening %s",audio_file);
		return;
	}
		
	vpb_wave_get_mode(rd, &mode);
	wave_get_size(rd, &size);

	ret = vpb_wave_open_write(&wr, tmp_file, mode);
	if (ret < 0) {
		mylog(LOG_INFO,"trim: error opening temp file %s",tmp_file);
		return;
	}
  	size -= lose;

	for(i=0; i<size; i+=N) {
		vpb_wave_read(rd, buf, N);
		vpb_wave_write(wr, buf, N);
	}

	vpb_wave_close_read(rd);
	vpb_wave_close_write(wr);

	ret = rename(tmp_file, audio_file);
	if (ret < 0) {
		mylog(LOG_INFO,"trim: error renaming %s: %s",audio_file,
		      strerror(errno));
		return;
	}
	unlink(tmp_file);
}

int arg_exists(int argc, char *argv[], char *arg) {
  int i;

  for(i=0; i<argc; i++)
    if (strcmp(argv[i],arg) == 0)
      return i;

  return 0;
}

int ctwaitforevent(int h,void *timer,int event_type,int data,int timeout_ms) {
  char      s[VPB_MAX_STR];
  int       ret, state;
  VPB_EVENT e;   

  vpb_timer_change_period(timer, timeout_ms);
  if (timeout_ms != 0)
    vpb_timer_start(timer);

  state = 1;
  while(state && !finito) {
    ret = vpb_get_event_ch_async(h, &e);

    if (ret == VPB_OK) {
      vpb_translate_event(&e, s); s[strlen(s)-1]=0;
      mylog(LOG_INFO,"%s",s);

      if ((e.type == event_type) && (e.data == data)) {
	state = 0;
      }
      if (e.type == VPB_TIMEREXP) {
	state = 0;
      }
    }
    else
      vpb_sleep(100);
  }
	  
  vpb_timer_stop(timer);	
  if (state == 0)
    return e.type;
  else
    return -1;
}

// Records CID samples to a buffer

void *rec_thread(void *pv) {
  THREAD_INFO *ti = (THREAD_INFO*)pv;

  // record buffer of samples between rings

  vpb_record_buf_start(ti->h, VPB_LINEAR);
  vpb_record_buf_sync(ti->h, (char*)ti->buf, sizeof(short)*CIDN);
  vpb_record_buf_finish(ti->h);

  return(NULL);
}
			    
