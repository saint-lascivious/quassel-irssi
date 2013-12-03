/*
   This file is part of QuasselC.

   QuasselC is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   QuasselC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with QuasselC.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE
#include <asm/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <iconv.h>
#include "quasselc.h"

void useless_printf(char *str, ...) {
	(void)str;
}

#if 0
#define dprintf(x...) printf(x)
#else
#define dprintf(x...) useless_printf(x)
#endif

static int highlight=0;

struct buffer {
	struct bufferinfo i;
	int lastseen;
	int marker;
	int displayed;
};
static struct buffer *buffers;
static int n_buffers;
static char *match;

int quassel_find_buffer_id(char *name, uint32_t network) {
	int i;
	for(i=0;i<n_buffers;++i) {
		if(buffers[i].i.id==(uint32_t)-1)
			continue;
		if(strcmp(buffers[i].i.name, name)==0 && buffers[i].i.network == network)
			return i;
	}
	return -1;
}

void irssi_send_message(GIOChannel* h, int buffer, char *message) {
	send_message(h, buffers[buffer].i, message);
}

void handle_backlog(struct message m) {
	if(!match)
		return;
	if(!strstr(m.content, match))
		return;
	char msg[512];
	char *nick=strdup(m.sender);
	if(index(nick, '!'))
		*index(nick, '!')=0;
	snprintf(msg, 511, "%s: %s", nick, m.content);
	free(nick);
	msg[511]=0;
}

char *stripname(char *str) {
	char *res=malloc(strlen(str));
	char *tmp=res;
	while(str[0]) {
		if(isalnum(*str)) {
			*tmp=*str;
			tmp++;
		}
		++str;
	}
	*tmp=0;
	return res;
}

extern void irssi_quassel_handle(void* arg, int msgid, int buffer_id, int network, char* buf, char* sender, int type, int flags, char* content);
void handle_message(struct message m, void *arg) {
	irssi_quassel_handle(arg, m.id, m.buffer.id, m.buffer.network, m.buffer.name, m.sender, m.type, m.flags, m.content);
}

void handle_sync(object_t o, function_t f, ...) {
	(void) o;
	va_list ap;
	char *fnc=NULL;
	char *net,*chan,*nick,*name;
	int netid,bufid,msgid;
	va_start(ap, f);
	switch(f) {
		/* BufferSyncer */
		case Create:
			bufid=va_arg(ap, int);
			netid=va_arg(ap, int);
			name=va_arg(ap, char*);
			dprintf("CreateBuffer(%d, %d, %s)\n", netid, bufid, name);
			if(bufid>=n_buffers) {
				buffers=realloc(buffers, sizeof(struct buffer)*(bufid+1));
				int i;
				for(i=n_buffers;i<=bufid;++i)
					buffers[i].i.id=-1;
				n_buffers=bufid+1;
			}
			buffers[bufid].i.network=netid;
			buffers[bufid].i.id=bufid;
			buffers[bufid].i.name=name;
			buffers[bufid].marker=0;
			buffers[bufid].lastseen=0;
			buffers[bufid].displayed=1;
			break;
		case MarkBufferAsRead:
			highlight=0;
			if(!fnc)
				fnc="MarkBufferAsRead";
		case Displayed:
			if(!fnc)
				fnc="BufferDisplayed";
			bufid=va_arg(ap, int);
			dprintf("%s(%d)\n", fnc, bufid);
			buffers[bufid].displayed=1;
			break;
		case Removed:
			if(!fnc)
				fnc="BufferRemoved";
		case TempRemoved:
			if(!fnc)
				fnc="BufferTempRemoved";
			bufid=va_arg(ap, int);
			buffers[bufid].displayed=0;
			dprintf("%s(%d)\n", fnc, bufid);
			break;
		case SetLastSeenMsg:
			if(!fnc)
				fnc="SetLastSeenMsg";
			bufid=va_arg(ap, int);
			msgid=va_arg(ap, int);
			buffers[bufid].lastseen=msgid;
			dprintf("%s(%d, %d)\n", fnc, bufid, msgid);
			break;
		case SetMarkerLine:
			if(!fnc)
				fnc="SetMarkerLine";
			bufid=va_arg(ap, int);
			msgid=va_arg(ap, int);
			buffers[bufid].marker=msgid;
			dprintf("%s(%d, %d)\n", fnc, bufid, msgid);
			break;
		/* IrcChannel */
		case JoinIrcUsers:
			net=va_arg(ap, char*);
			chan=va_arg(ap, char*);
			int size=va_arg(ap, int);
			char **users=va_arg(ap, char**);
			char **modes=va_arg(ap, char**);
			if(size==0)
				break;
			if(size>1) {
				dprintf("Too many users joined\n");
				break;
			}
			dprintf("JoinIrcUser(%s, %s, %s, %s)\n", net, chan, users[0], modes[0]);
			break;
		case AddUserMode:
			if(!fnc)
				fnc="AddUserMode";
		case RemoveUserMode:
			if(!fnc)
				fnc="RemoveUserMode";
			net=va_arg(ap, char*);
			chan=va_arg(ap, char*);
			nick=va_arg(ap, char*);
			char *mode=va_arg(ap, char*);
			dprintf("%s(%s, %s, %s, %s)\n", fnc, net, chan, nick, mode);
			break;
		/* IrcUser */
		case SetNick2:
			if(!fnc)
				fnc="SetNick";
		case Quit:
			if(!fnc)
				fnc="Quit";
			net=va_arg(ap, char*);
			nick=va_arg(ap, char*);
			dprintf("%s(%s, %s)\n", fnc, net, nick);
			break;
		case SetNick:
			if(!fnc)
				fnc="SetNick";
		case SetServer:
			if(!fnc)
				fnc="SetServer";
		case SetRealName:
			if(!fnc)
				fnc="SetRealName";
		case PartChannel:
			if(!fnc)
				fnc="PartChannel";
			net=va_arg(ap, char*);
			nick=va_arg(ap, char*);
			char *str=va_arg(ap, char*);
			dprintf("%s(%s, %s, %s)\n", fnc, net, nick, str);
			break;
		case SetAway:
			net=va_arg(ap, char*);
			nick=va_arg(ap, char*);
			int away=va_arg(ap, int);
			dprintf("setAway(%s, %s, %s)\n", net, nick, away ? "away" : "present");
			break;
		/* Network */
		case AddIrcUser:
			net=va_arg(ap, char*);
			char *name=va_arg(ap, char*);
			dprintf("AddIrcUser(%s, %s)\n", net, name);
			break;
		case SetLatency:
			net=va_arg(ap, char*);
			int latency=va_arg(ap, int);
			dprintf("SetLatency(%s, %d)\n", net, latency);
			break;
	}
	va_end(ap);
}
