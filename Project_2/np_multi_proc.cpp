#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <regex.h>
#include <algorithm>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/shm.h>

#define shm_clientkey 23456
#define shm_multicastmsgkey 23457

using namespace std;

class MulticastMsg
{
public:
	char mmsg[1000];
};

class User
{
public:
	char ipaddr[30];
	char name[30];
	bool valid;
	int link1;
	int link2;
	bool fin;
	int upid;
};

struct Procpipe
{
    int cnt;
    int rw[2];  
};

int working;
int shm0, shm1;
const int blackhole = open("/dev/null", O_RDWR);
size_t cmdl_size = 15000;
User *client_arr;
MulticastMsg *MMessage;

void string_to_cstring(char* input, string ele);

string cstring_to_string(char* input);

bool uppisthere(int writer, int reader);

void sigchild_handle(int signum);

void sigint_handle(int signum);

void NPSHELL();

void init_clients();

void multicast(int* rcvr, char* msg);

void multicast_trigger(int signum);

void shellsigchild_handle(int signum);

void parse_into_sections(string cmdl, vector<string>& cmdlSection);

void userpipe_handler(int uppwriter, int& uppreadfd, int uppreader, 
					  int& uppwritefd, char* cmdl, bool& upipefromerr, bool& upipetoerr, 
					  char* userpipefifo);

void execution(vector<string> ttargs, int uppwritefd, char* userpipefifo);


int main(int argc, char *argv[])
{
	if (argc != 2)
		return 0;

	clearenv();

	int msock, ssock;
	if ((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		cerr << "Fail to create socket.\n";
		exit(0);
	}
	
	const int enable = 1;
	if (setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    	cerr << "setsockopt(SO_REUSEADDR) failed";

	struct sockaddr_in serv_addr, cli_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	unsigned short ptnum = (unsigned short)atoi(argv[1]);
    serv_addr.sin_port = htons(ptnum);
	if (bind(msock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		cerr << "Fail to bind.\n";
		exit(0);
	}
	listen(msock, 30);

	shm0 = shmget(shm_clientkey, sizeof(User)*30, 0666 | IPC_CREAT);
    shm1 = shmget(shm_multicastmsgkey, sizeof(MulticastMsg), 0666 | IPC_CREAT);
	client_arr = (User*)shmat(shm0, (char*)0, 0);
	MMessage = (MulticastMsg*)shmat(shm1, (char*)0, 0);	
	
	signal(SIGCHLD, sigchild_handle);
	signal(SIGINT, sigint_handle);
	init_clients();

	while(1)
	{
		socklen_t cli_len = sizeof(cli_addr);
		ssock = accept(msock, (sockaddr *)&cli_addr, &cli_len);
		for (working = 0; working < 30; working++)
		{
			if (!client_arr[working].valid)
				break;
		}
	redo:
		int status;
		pid_t cpid = fork();			
		if(cpid < 0)
		{
			while(waitpid(-1,&status,WNOHANG) > 0){}
			goto redo;
		}

		if(cpid != 0) 
		{
			client_arr[working].valid = true;
			char tc[INET_ADDRSTRLEN];
			strcpy(client_arr[working].name, "(no name)");
            inet_ntop(AF_INET, &(cli_addr.sin_addr), tc, INET_ADDRSTRLEN);
			int ptnum = ntohs(cli_addr.sin_port);
			string strip = cstring_to_string(tc) +":"+to_string(ptnum);
			strcpy(client_arr[working].ipaddr, strip.data());
			client_arr[working].upid = cpid;
			close(ssock);
		}
		else //child
		{
			close(0);
			dup2(ssock, 0);
			close(1);
			dup2(ssock, 1);
			close(2);
			dup2(ssock, 2);
			close(ssock);
			close(msock);
			usleep(10);
			NPSHELL();
			exit(0);
		}
	}
}

void sigint_handle(int signum)
{
	shmdt(MMessage); //detach
	shmdt(client_arr);
	shmctl(shm1, IPC_RMID, (shmid_ds*)0); //remove
	shmctl(shm0, IPC_RMID, (shmid_ds*)0);
	//cout << "exitting it";
	exit(1);
}

void init_clients()
{
	for(int i = 0; i < 30; i++)
	{
		client_arr[i].valid = false;
		client_arr[i].fin = false;
		client_arr[i].upid = 0;
	}
}

void sigchild_handle(int signum)
{
	int status;
	while(waitpid(-1, &status, WNOHANG) > 0){}

	for(int g = 0; g < 30; ++g)
	{
		if(client_arr[g].valid)
		{
			if(kill(client_arr[g].upid, 0) < 0)  //user[g] just exited
			{
				client_arr[g].valid = false;
				string tstr = "*** User '" + cstring_to_string(client_arr[working].name) + "' left. ***\n";

				string_to_cstring(MMessage->mmsg, tstr);
				for(int h = 0; h < 30; ++h)
				{
					if(client_arr[h].valid)
						kill(client_arr[h].upid, SIGUSR1); 
				}

				for(int h = 0; h < 30; ++h)
				{
					int t1 = g+1;
					int t2 = h+1;
					if(uppisthere(t1, t2))
					{
						char fifoname[30];
						string fifostr = "user_pipe/" + to_string(t1) + "_" + to_string(t2);
						string_to_cstring(fifoname, fifostr);
						remove(fifoname);
					}

					if(uppisthere(t2,t1))
					{
						char fifoname[30];
						string fifostr = "user_pipe/" + to_string(t2) + "_" + to_string(t1);
						string_to_cstring(fifoname, fifostr);
						remove(fifoname);
					}
				}
				break;
			}
		}
	}
}

bool uppisthere(int writer, int reader)
{
	string tstr = "user_pipe/" + to_string(writer) + "_" +to_string(reader);
	char* tcstr = (char*)malloc(sizeof(char)*(tstr.size()+1));
	string_to_cstring(tcstr, tstr);
	if(access(tcstr ,F_OK) != -1) return true;
	return false;
}

void multicast(int* rcvr, char* msg)
{
	// for(int i =0;i <= strlen(msg); i++) cout << (int)msg[i] << ' ';

 	// cout << "\nm is : " << msg  << strlen(msg) << endl;
	// cout << "MMessage->mmsg is : " << MMessage->mmsg << strlen(MMessage->mmsg) << endl;

	// for(int i = 0; i < strlen(msg); i++ ) 
	// 	if(msg[i] == '\n') cout << "EEEEEOOOOOOTTTTTTTTTT: i = " << i << endl; 

	strcpy(MMessage->mmsg, msg);
	if (rcvr == NULL)
	{
		for (int i = 0; i < 30; i++)
		{
			if (client_arr[i].valid)
				kill(client_arr[i].upid, SIGUSR1);
		}
	} 
	else 
		kill(client_arr[*rcvr].upid, SIGUSR1);
}
		
void multicast_trigger(int signum)
{
	write(STDOUT_FILENO, MMessage->mmsg, strlen(MMessage->mmsg));
}

void shellsigchild_handle(int signum)
{
	//cout << "child ended here";
	int status;
	while(waitpid(-1,&status,WNOHANG) > 0){}
}

void parse_into_sections(string cmdl, vector<string>& cmdlSection)
{
	int from = 0;
	while (cmdl != "")
	{
		int found = 0;
		for(int i = 0; i < cmdl.size(); ++i)
		{
			int j = i+1;
			if((cmdl[i] == '|' || cmdl[i] == '!') && cmdl[i+1] != ' ')
			{
				while(j < cmdl.size() && cmdl[j] != ' ') j++; //breaks if j out of bound or cmdline[j] is ' '
		 		j--;
		 		string tempss = cmdl.substr(from, j-from+1);
		 		tempss = tempss.substr(tempss.find_first_not_of(" ", 0));
				cmdlSection.push_back(tempss);
				cmdl = cmdl.substr(j+1);
				found = 1;
				break;
			}
		}

		if(!found)
		{
		    cmdl = cmdl.substr(cmdl.find_first_not_of(" ", 0));
			cmdlSection.push_back(cmdl);
			break;
		}
	}
	//for(auto i: cmdlSection) cout << i << endl;
}

void userpipe_handler(int uppwriter, int& uppreadfd, int uppreader, 
					  int& uppwritefd, char* cmdl, bool& upipefromerr, bool& upipetoerr, 
					  char* userpipefifo)
{
	if(uppwriter > 0)
	{
		if(uppwriter < 1 || uppwriter > 30 || !client_arr[uppwriter-1].valid)
		{
			cerr << "*** Error: user #" << uppwriter << " does not exist yet. ***\n";
			upipefromerr = 1;
			// close(STDIN_FILENO);
			// dup2(blackhole, STDIN_FILENO);
		}
		else
		{
			if(!uppisthere(uppwriter,working+1))
			{
				cerr << "*** Error: the pipe #" << uppwriter << "->#" << working+1 << " does not exist yet. ***\n";
				upipefromerr = 1;
				// close(STDIN_FILENO);
				// dup2(blackhole, STDIN_FILENO);
			}
			else
			{
				string str1 = "user_pipe/"+to_string(uppwriter)+"_"+to_string(working+1);
				string str2 = "user_pipe/!"+to_string(uppwriter)+"_"+to_string(working+1);
				char cstr1[50];
				char cstr2[50];
				string_to_cstring(cstr1, str1);
				string_to_cstring(cstr2, str2);
				rename(cstr1, cstr2);
				uppreadfd = open(cstr2, O_RDONLY);
				string tstr = "";
				tstr = "*** " + cstring_to_string(client_arr[working].name) + " (#" + to_string(working+1) 
							  + ") just received from " + cstring_to_string(client_arr[uppwriter-1].name) 
						      + " (#" + to_string(uppwriter) + ") by '" + cstring_to_string(cmdl) + "' ***\n";
				char tempm[200];
				string_to_cstring(tempm, tstr);
				multicast(NULL, tempm);
				usleep(10);
			}
		}
	}

	if(uppreader > 0)
	{
		if(uppreader < 1 || uppreader > 30 || !client_arr[uppreader-1].valid)
		{
			cerr << "*** Error: user #" << uppreader << " does not exist yet. ***\n";
			upipetoerr = 1;
			// close(STDOUT_FILENO);
			// dup2(blackhole, STDOUT_FILENO);
		}
		else
		{
			if(uppisthere(working+1, uppreader))
			{
				cerr << "*** Error: the pipe #" << working+1 << "->#" <<uppreader << " already exists. ***\n"; 
				upipetoerr = 1;
				// close(STDOUT_FILENO);
				// dup2(blackhole, STDOUT_FILENO);
			}
			else
			{
				string str = "user_pipe/" + to_string(working+1) + "_" + to_string(uppreader);
				string_to_cstring(userpipefifo, str);
				uppwritefd = open(userpipefifo, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
				string sss = "*** " + cstring_to_string(client_arr[working].name) + " (#"+to_string(working+1)
									+ ") just piped '" +cstring_to_string(cmdl) + "' to " 
									+ cstring_to_string(client_arr[uppreader-1].name) + " (#"+to_string(uppreader)
									+ ") ***\n";
				char tempm[200];
				string_to_cstring(tempm, sss);
				multicast(NULL, tempm);
			}
		}
	}
}

void string_to_cstring(char* input, string ele)
{
	int i;
	for(i = 0; i < ele.size(); i++)
	{
		input[i] = ele[i];
	}
	input[i] = '\0';
}

string cstring_to_string(char* input)
{
	string ele = "";
	for(int i = 0; i < strlen(input); i++)
	{
		if(input[i] != '\0') ele += input[i];
		else break;	
	}
	return ele;
}

void execution(vector<string> ttargs, int uppwritefd, char* userpipefifo)
{
	//exit(0);
	vector<const char*> targs;
	for(int i = 0; i < ttargs.size(); i++)
		targs.push_back(ttargs[i].c_str());

	targs.push_back(NULL);
	const char** args = &targs[0];

	if(execvp(ttargs[0].c_str(),(char* const*)args) < 0)
	{
		cerr << "Unknown command: [" << ttargs[0] << "].\n";
		// if(red)
		// {
		// 	char* tempf = (char*)malloc(sizeof(char)*(filename.size()+1));
		// 	string_to_cstring(tempf, filename);
		// 	remove(tempf); 
		// }
		if(uppwritefd != -1)
		{
			remove(userpipefifo);
		}
		exit(1);
	}
}

void NPSHELL()
{
	signal(SIGCHLD, shellsigchild_handle);
    signal(SIGUSR1, multicast_trigger);
	char welcomemsg[] = "****************************************\n** Welcome to the information server. **\n****************************************";
	cout << welcomemsg << endl;
	string entermsg = "*** User '" + cstring_to_string(client_arr[working].name) + "' entered from "
							  + cstring_to_string(client_arr[working].ipaddr) + ". ***\n";
	char tempm[300];
	string_to_cstring(tempm, entermsg);
	multicast(NULL, tempm);

	clearenv();
	setenv("PATH", "bin:.", 1);
    vector <Procpipe> ppps;
	while(1)
	{
		string cmdlinestr;
		cout<<"% ";
		getline(cin, cmdlinestr);
		
		if (cmdlinestr[cmdlinestr.size()-1] == 13)
		{
			cmdlinestr = cmdlinestr.substr(0, cmdlinestr.size()-1);
			cmdlinestr = cmdlinestr.substr(0, cmdlinestr.find_last_not_of(' ')+1);
		}
		if(cmdlinestr.empty() || cmdlinestr.find_first_not_of(" ",0) == string::npos)
			continue;		
		//for(int i = 0; i < cmdlinestr.size(); i++) cout << (int)cmdlinestr[i] << ' ';

		//parse into section here
		vector<string> cmdsec;
		parse_into_sections(cmdlinestr, cmdsec);
		for(auto ele: cmdsec)	
		{
			if(ele.size() == 0 || ele.find_first_not_of(" ", 0) == string::npos) continue;

			char* cmdseccstr = (char*)malloc(300*sizeof(char));
			string_to_cstring(cmdseccstr, ele);

			bool red = false;
			int ppt = -1;
			int ppcnt = 0;
			int uppreader = 0;
			int uppwriter = 0;
			int uppreadfd = -1;
			int uppwritefd = -1;
			string fredname = "";			
			char defaultcmd[cmdl_size];
			char unchanged[cmdl_size];
			vector<vector<string> > single_cmd;
			strcpy(defaultcmd,cmdseccstr);
			strcpy(unchanged, cmdseccstr);

			if(ele == "exit")
			{
				string leavestr = "*** User '" + cstring_to_string(client_arr[working].name) 
											   + "' left. ***\n";
				char leavecstsr[200];
				string_to_cstring(leavecstsr, leavestr);
				multicast(NULL, leavecstsr);
				client_arr[working].valid = false;
				
				for(int i = 0; i < 30; i++)
				{
					if(uppisthere(working+1, i+1))
					{
						string uppname = "user_pipe/" + to_string(working+1) + "_" + to_string(i+1);
						remove(uppname.data());
					}
					if(uppisthere(i+1, working+1))
					{
						string uppname = "user_pipe/" + to_string(i+1) + "_" + to_string(working+1);
						remove(uppname.data());
					}
				}
				shmdt(client_arr);
				shmdt(MMessage);
				exit(0);
			}


			int ind=0;
			char* parser;
			parser = strtok(cmdseccstr," ");
			vector<string> tvec;
			tvec.clear();
			single_cmd.push_back(tvec);
			while(parser != NULL)
			{
				if (parser[0] == '>')
				{
					//cout << "red here";
					//for(int i = 0; i < tvec.size(); i++){};
					if(strlen(parser) == 1)
					{ 
						red = 1;
						parser = strtok(NULL, " ");
						fredname = string(parser);
						parser = strtok(NULL, " ");
						if(parser == NULL) 
							break;
						if(parser[0]=='<' || parser[0]=='!' || parser[0] == '|') 
							continue;
					}
					else
					{ 
						parser[0]=' ';
						uppreader = atoi(parser);
						parser = strtok(NULL, " ");
						continue;
					}
				}
				else if(parser[0] == '<')
				{
					parser[0] = ' ';
					uppwriter = atoi(parser);
					parser = strtok(NULL, " ");
					continue;
				}
				else if(parser[0] == '|' || parser[0] == '!')
				{
					if(strlen(parser) > 1)
					{ 
						if(parser[0] == '!') //err pipe
						{
							ppt = 1;
						}
						else //normal pipe
						{
							ppt = 0;
						}
						parser[0]=' ';
						ppcnt = atoi(parser);
					}
					else
					{
						ind++;
						vector<string> ttvec;
						ttvec.clear();
						single_cmd.push_back(ttvec);
					}
					parser = strtok(NULL, " ");
					continue;
				}
				single_cmd[ind].push_back(string(parser));
				parser = strtok(NULL, " ");
			}  

			for(int i = 0;i < ppps.size(); i++)
				ppps[i].cnt--;

			bool fromsomenpp = false;
			Procpipe writepp, readpp;
			if(ppt != -1)
			{
				int nppfd;
				bool need_new_pipe = true;
				for(int i = 0; i < ppps.size(); i++)
				{
					if(ppps[i].cnt == ppcnt)
					{
						need_new_pipe = false;
						nppfd = i;
						break;
					}
				}
				if(need_new_pipe)
				{
					writepp.cnt = ppcnt;
					pipe(writepp.rw);
					ppps.push_back(writepp);
				}
				else
				{ 
					writepp = ppps[nppfd];
				}
			}
			for(int i = 0; i < ppps.size(); i++)
			{
				if(ppps[i].cnt == 0)
				{
					fromsomenpp = true;
					readpp = ppps[i];
					ppps.erase(ppps.begin()+i);
					close(readpp.rw[1]);
					break;
				}
			}

			bool pipefromerr = 0;
			bool pipetoerr = 0;
			char* userpipefifo = (char*)malloc(sizeof(char)*22);
			userpipe_handler(uppwriter, uppreadfd, 
							uppreader, uppwritefd, unchanged, 
							pipefromerr, pipetoerr, userpipefifo);

			
			if(single_cmd[0][0] == "who")
			{
				cout << "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
				for (int i = 0; i < 30; i++)
				{
					if(client_arr[i].valid)
					{
						cout << to_string(i+1) << '\t' << client_arr[i].name << '\t' << client_arr[i].ipaddr;
						if (i == working)
							cout << "\t<-me";
						cout << "\n";
					}
				}
				free(cmdseccstr);
				continue;
			}

			if(single_cmd[0][0] == "tell")
			{
				int to = atoi(single_cmd[0][1].data());

				string content = "";
				for(int j = 2; j < single_cmd[0].size(); j++)
					content = content + single_cmd[0][j] + ' ';
				content = content.substr(0, content.size()-1);

				int rcvr = to-1;
				if(rcvr <= 29 && rcvr >= 0 && client_arr[rcvr].valid)
				{
					string str = "*** " + cstring_to_string(client_arr[working].name) 
										+ " told you ***: " + content + '\n';
					char tempm[1000];
					string_to_cstring(tempm, str);
					multicast(&rcvr, tempm);
				} 
				else cout << "*** Error: user #" << rcvr+1 << " does not exist yet. ***\n";

				free(cmdseccstr);
				continue;
			}

			if(single_cmd[0][0] == "yell")
			{
				string content = "";
				for(int j = 1; j < single_cmd[0].size(); j++)
					content = content + single_cmd[0][j] + ' ';
				content = content.substr(0, content.size()-1);

				string str = "*** " + cstring_to_string(client_arr[working].name) + " yelled ***: "
						+ content + "\n"; 
				char tempm[1000];
				string_to_cstring(tempm, str);
				multicast(NULL, tempm);

				free(cmdseccstr);
				continue;
			}

			if(single_cmd[0][0] == "name")
			{
				char tempm[200];
				bool namable = 1;
				for (int i = 0; i < 30; i++)
				{
					if (i == working)
						continue;
					bool same = (single_cmd[0][1] == cstring_to_string(client_arr[i].name));
					if(same && client_arr[i].valid)
					{
						string tstr = "*** User '" + single_cmd[0][1] + "' already exists. ***\n"; 
						string_to_cstring(tempm, tstr);
						multicast(&working, tempm);
						namable = 0;
						break;
					}
				}
				if(namable)
				{
					strcpy(client_arr[working].name, single_cmd[0][1].data());
					string tstr = "*** User from "+cstring_to_string(client_arr[working].ipaddr)
									+" is named '" +cstring_to_string(client_arr[working].name)+ "'. ***\n";
					string_to_cstring(tempm, tstr);
					multicast(NULL, tempm);
				}

				free(cmdseccstr);
				continue;
			}

			if(single_cmd[0][0] == "printenv")
			{
				char *en = getenv(single_cmd[0][1].c_str());
				if(en != NULL) cout << en << endl;			
				free(cmdseccstr);
				continue;
			}
			
			if(single_cmd[0][0] == "setenv")
			{
				if(single_cmd[0].size() >= 3) 
				setenv(single_cmd[0][1].data(), single_cmd[0][2].data(), 1);
				free(cmdseccstr);
				continue;
			}

			int inppfd = 0;
			if(fromsomenpp) //inputs from some number pipe
			{ 
				inppfd = readpp.rw[0];
			}
			if(uppreadfd != -1) //inputs from some user pipe
			{ 
				inppfd = uppreadfd;
			}

			int redf;
			if(red)
				redf = open(fredname.data(), O_CREAT|O_WRONLY|O_TRUNC,S_IRUSR|S_IWUSR);
			pid_t cpid;
			int temp_pp[2];
			for(int i = 0; i < single_cmd.size(); i++)
			{
				if(i < (single_cmd.size()-1)) //not last cmd so parent creates pipe
				{ 
					if(pipe(temp_pp) == -1) cout << "can't create pipe\n";
				}

				while((cpid=fork())<0)
					usleep(1000);

				if(cpid == 0) //child
				{
					if(inppfd > 0)
					{     
						close(STDIN_FILENO);         
						dup2(inppfd,STDIN_FILENO);
						close(inppfd); 
					}
					if(i == 0 && pipefromerr) 
					{
						close(STDIN_FILENO);
						dup2(blackhole, STDIN_FILENO);
						close(blackhole);
					}

					if(i < (single_cmd.size()-1))
					{     
						close(STDOUT_FILENO);          
						dup2(temp_pp[1],STDOUT_FILENO);  
						close(temp_pp[1]);    
						close(temp_pp[0]);    
					}
					else if(i == (single_cmd.size()-1)) //last cmd
					{    
						if(red) //file red
						{
							close(STDOUT_FILENO);
							dup2(redf,STDOUT_FILENO);
							close(redf);
						}
						if(ppt > -1) //output piped
						{
							close(STDOUT_FILENO);
							dup2(writepp.rw[1],STDOUT_FILENO);
							if(ppt == 1) //err pipe
							{
								close(2);
								dup2(writepp.rw[1],STDERR_FILENO);
							}
							close(writepp.rw[1]);
							close(writepp.rw[0]);
						}
						if(pipetoerr)
						{
							close(STDOUT_FILENO);
							dup2(blackhole, STDOUT_FILENO);
							//close(blackhole);
						}
						else if(uppwritefd != -1)
						{
							//cout << "here";
							close(STDOUT_FILENO);
							dup2(uppwritefd,STDOUT_FILENO);
							close(uppwritefd);
						}  	
					}
					execution(single_cmd[i], uppwritefd, userpipefifo);
				}
				else //parent
				{ 
					if(red && (i == (single_cmd.size()-1)))
						close(redf);
					if(inppfd > 0)
						close(inppfd);   
					if(i < (single_cmd.size()-1))
					{
						close(temp_pp[1]);    
						inppfd = temp_pp[0]; 
					}

					if(i == (single_cmd.size()-1) && uppwritefd != -1)
						close(uppwritefd);
				}
			}

			if(ppt == -1) //no pipe
			{ 
				int status;
				waitpid(cpid, &status, 0);

				if(uppreadfd != -1)
				{
					string tstr = "user_pipe/!" +to_string(uppwriter)+ "_" + to_string(working+1);
					remove(tstr.data());
				}
			}
			else 
			{
				int status;
				if(ppcnt > 0) waitpid(cpid, &status, 0);
			}
			free(cmdseccstr);
		}
	}
    return;
}
