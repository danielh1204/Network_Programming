#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sstream>
#include <fcntl.h>
#include <netinet/in.h>
#include <vector>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace std;

class single_cmd{
public:
	string parsed;
	char symb;
	int nppcnt;
	vector<string> args;
	void setmember(string com, char m, int n); 
	int filetp();
	void setargs();
	int in_index, out_index;
	int link1;
	int link2;
	bool piped_in, piped_out;
	int ppt; // 0: no pp, 1: normal pp, 2: err pp
};

class ProcPipe{
public:
    int rw[2];
    int cnt;
};

void single_cmd::setmember(string tt, char ts, int tc)
{
	symb = ts;
	nppcnt = tc;
	parsed = tt;
}

void single_cmd::setargs()
{
	vector<string> targs;
	string tt;
	stringstream strs(parsed);
	while(strs >> tt)
		targs.push_back(tt);
	args = targs;
}

int single_cmd::filetp()
{
	string aa = args.at(0);

	if(aa == "printenv")
		return 1;
	else if(aa == "setenv")
		return 2;
	else if(aa == "exit" || aa == "EOF")
		return 3;
	else
		return 4;
}

vector<ProcPipe> ppps;
vector<single_cmd> cmdvec;

void sigchld_handle(int signum);

void parse_cmdsec(string cmdl);

void parse_into_sections(string cmdl, vector<string>& cmdlSection);

void create_pipe(int ind);

void para_fix(char *buf[], int se, int ind);

int findinvec(vector<string> &vec, string target);

void execution(int i);

int NPSHELL() ;
