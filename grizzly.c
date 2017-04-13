#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/*** data ***/
typedef struct erow{
	char* chars;
	char* render;
	int size;
	int rsize;
} erow;

typedef struct editorConfig{
	struct termios orig_termios;
	erow* row;
	char* filename;
	char statusmsg[80];
	time_t statusmsg_time;
	int screenrows, screencols;
	int cx, cy;
	int rx;
	int numrows;
	int rowoff, coloff;
}editorConfig;

typedef struct abuf{
	char* b;
	int len;
}abuf;

/*** function defs ***/
void disableRawMode();
void enableRawMode();
void die(const char *s);
void  editorProcessKeypress();
int editorReadKey();
void editorRefreshScreen();
void editorDrawRows();
int getWindowSize(int* rows, int* cols);
void initEditor();
int getCursorPosition(int* rows, int* cols);
void editorMoveCursor(int c);
void editorOpen(char* filename);
void editorAppendRow(char* s, size_t len);
void editorScroll();
void editorUpdateRow(erow* row);
int editorRowCxToRx(erow* row, int cx);
void editorDrawStatusBar(abuf* ab);
void editorSetStatusMessage(const char* fmt, ...);

/*** globals ***/
editorConfig E;

/*** definitions ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** init ***/

int main(int argc, char* argv[]){
	enableRawMode();
	initEditor();
	if(argc >= 2)
		editorOpen(argv[1]);

	while(1){ //infinite loop
		editorProcessKeypress();
		editorRefreshScreen();
	}
	return 0;
}

void initEditor(){
	E.cx = 0; //initialize index of cursor
	E.cy = 0;
	E.rx = 0;
	E.numrows = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.row = NULL;
	E.filename = NULL;
	E.statusmsg[0] = NULL;
	E.statusmsg_time = 0;

	if(getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
	E.screenrows--;
}


/*** append buffer***/
void abAppend(abuf* ab, const char* s, int len){
	char* new = realloc(ab->b, ab->len + len); //reallocate double the mem
	if (new == NULL)
		return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;	//double the length
}

void abFree(abuf *ab) {
  free(ab->b);		//deallocate the memory used by abuf
}

/*** terminal ***/
void enableRawMode(){
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
		die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disableRawMode(){
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    	die("tcsetattr");
}

void die(const char *s){
	write(STDOUT_FILENO, "\x1b[2J", 4);
 	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

int getCursorPosition(int* rows, int* cols){
	char buf[32];
	unsigned int i;

	while (i < sizeof(buf) - 1) { //don't touch last char
	    if (read(STDIN_FILENO, &buf[i], 1) != 1)
	    	break;
	    if (buf[i] == 'R')
	    	break;
	    i++;
  	}
	buf[i] = '\0';	//make sure it's null terminated
	if (buf[0] != '\x1b' || buf[1] != '[') 
		return -1;
  	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
  		return -1;
  	return 0;
}

int getWindowSize(int* rows, int* cols){
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 && ws.ws_col == 0) {
    	if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
    		return -1;
    	return getCursorPosition(rows, cols);
    } else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
    }
}

/*** input ***/
void editorMoveCursor(int key) {
	erow* row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	
	switch (key) {
		case ARROW_LEFT:
		  	if (E.cx != 0)
		    	E.cx--;
		    else if (E.cy > 0){
		    	E.cy--;
		    	E.cx = E.row[E.cy].size;
		    }
		  	break;
		case ARROW_RIGHT:
		  	if (row && E.cx < row->size) //let it be equal
		  		E.cx++;
		  	else if (row && E.cx == row->size){
		  		E.cy++;
		  		E.cx = 0;
		  	}
		  	break;
		case ARROW_UP:
			if (E.cy != 0)
				E.cy--;
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows)
		    	E.cy++;
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen)
		E.cx = rowlen;
}

void  editorProcessKeypress(){
	int c = editorReadKey();

	switch(c){
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
  			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;

		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			if(E.cy < E.numrows) //if row exists
				E.cx = E.row[E.cy].size;
			break;

		case PAGE_UP:
		case PAGE_DOWN:
		{
			if(c == PAGE_UP){
				E.cy = E.rowoff;
			} else if(c == PAGE_DOWN){
				E.cy = E.rowoff + E.screenrows - 1;
				if(E.cy > E.numrows)
					E.cy = E.numrows;
			}
		}
			break;
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_RIGHT:
		case ARROW_LEFT:
			editorMoveCursor(c);
			break;
	}
}

int editorReadKey(){
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) == -1){
		if(nread == -1 && errno != EAGAIN)
			die("read");
	}
	if (c == '\x1b') {
		char seq[3];
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';
		if (seq[0] == '[') {
			 if (seq[1] >= '0' && seq[1] <= '9') {
        		if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        		if (seq[2] == '~') {
          			switch (seq[1]) {
          				case '1': return HOME_KEY;
          				case '3': return DEL_KEY;
          				case '4': return END_KEY;
            			case '5': return PAGE_UP;
            			case '6': return PAGE_DOWN;
          				case '7': return HOME_KEY;
          				case '8': return END_KEY;
          			}
        		}
      		} else {
      			switch (seq[1]) {
		        case 'A': return ARROW_UP;
		        case 'B': return ARROW_DOWN;
		        case 'C': return ARROW_RIGHT;
		        case 'D': return ARROW_LEFT;
		        case 'H': return HOME_KEY;
		        case 'F': return END_KEY;
        	}
     	}
    } else if(seq[0] == 'O'){
    	switch (seq[1]){
    		case 'H': return HOME_KEY;
    		case 'F': return END_KEY;
    	}
    }
	return '\x1b';
 	} else {
    	return c;
 	}
}

/*** output ***/
void editorSetStatusMessage(const char* fmt, ...){
	va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

void editorRefreshScreen() {
	editorScroll();
	abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
 	abAppend(&ab, "\x1b[H", 3);

 	editorDrawRows(&ab);
 	editorDrawStatusBar(&ab);

 	char buf[32];
 	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
 		(E.rx - E.coloff) +1);
 	abAppend(&ab, buf, strlen(buf));

 	abAppend(&ab, "\x1b[?25h", 6);

 	write(STDOUT_FILENO, ab.b, ab.len);
 	abFree(&ab);
}

void editorScroll(){
	E.rx = 0;
	if(E.cy < E.numrows)
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	
	if(E.cy < E.rowoff){
		E.rowoff = E.cy;
	}
	if(E.cy >= E.rowoff + E.screenrows){
		E.rowoff = E.cy - E.screenrows + 1;
	}
	if(E.rx < E.coloff)
		E.coloff = E.rx;
	if(E.rx >= E.coloff + E.screencols)
		E.coloff = E.rx - E.screencols + 1;
}

void editorDrawStatusBar(abuf* ab){
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];

	int len = snprintf(status, sizeof(status), "%.20s - %d lines",
		E.filename ? E.filename : "[No Name]", E.numrows);
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
		E.cy + 1, E.numrows);
	if(len > E.screencols)
		len = E.screencols;
	
	abAppend(ab, status, len);

	while(len < E.screencols){
		if(E.screencols - len == rlen){
			abAppend(ab, rstatus, rlen);
			break;
		} else {
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m", 3);
}

void editorDrawRows(abuf* ab){
	int y;
	for(y = 0; y < E.screenrows; y++){
		int filerow = y + E.rowoff;
		if(filerow >= E.numrows){
			if(E.numrows == 0 && y == E.screenrows / 3){
				char welcome[80];
				int  welcomelen = snprintf(welcome, sizeof(welcome),
					"Kilo Editor -- version %s", KILO_VERSION);
				int padding;
				if(welcomelen > E.screencols)
					welcomelen = E.screencols;
				padding = (E.screencols - welcomelen) / 2;
				if(padding){ //if there is padding, print ~
					abAppend(ab, "~", 1);
					padding--;
				}
				while(padding--) //add padding to the line
					abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomelen);
			} else {
				abAppend(ab, "~", 1); //just a normal line
			}
		} else{
			int len = E.row[filerow].rsize - E.coloff;
			if(len < 0)
				len = 0;
			if(len > E.screencols)
				len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}
			abAppend(ab, "\x1b[K", 3);
			abAppend(ab, "\r\n", 2);
	}
}

/*** file i/o ***/
void editorOpen(char* filename){
	free(E.filename);
	E.filename = strdup(filename);

	FILE *fp = fopen(filename, "r");
	if (!fp)
		die("fopen");
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
	  	                       line[linelen - 1] == '\r'))
	  	linelen--;
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
}

/*** row ops***/
void editorAppendRow(char* s, size_t len){
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
	
	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].rsize = 0;
	E.row[at].chars = malloc(len + 1); //each char is 1 byte
	E.row[at].render = NULL;

	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	editorUpdateRow(&E.row[at]); //set render

	E.numrows++;
}

void editorUpdateRow(erow* row){
	int tabs = 0;
	int j;
	for(j = 0; j < row->size; j++)
		if(row->chars[j] == '\t')
			tabs++;

	free(row->render); //reset mem
	row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1); //reallocate

	int idx = 0;
	for(j = 0; j < row->size; j++){
		if(row->chars[j] == '\t'){
			row->render[idx++] = ' ';
			while(idx % KILO_TAB_STOP != 0)
				row->render[idx++] = ' ';
		}
		else
			row->render[idx++] = row->chars[j];
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

int editorRowCxToRx(erow* row, int cx){
	int rx = 0;
	int j;
	for(j = 0; j < cx; j++){ //0 to cursor
		if(row->chars[j] == '\t')
			rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
		rx++;
	}
	return rx;
}