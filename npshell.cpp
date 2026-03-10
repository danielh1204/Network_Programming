#include <iostream>
#include <string.h>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <regex>
#include <cstdlib>

using namespace std;

class single_cmd{
public:
	bool piped_in, piped_out;
	int in_index, out_index;
	string filename;
	vector<string> args;
	int ppt; 
};

void execution(single_cmd& single_cmd);

void parse_into_sections(string cmdl, vector<string>& cmdlSection);

void preparse(string cmdlsec, vector<single_cmd>& cmdvec);

void parse_single_cmd(single_cmd& single_cmd);

void para_fix(vector<string> &targs, char *arr[], int sz);

void create_pipes(single_cmd& single_cmd);

static regex expression("[|!][0-9]+");
vector<int> cnt;
vector<int*> rw;

int main(){
	setenv("PATH", "bin:.", 1);

	string cmdl;
	vector<single_cmd> cmdvec;
	bool firc = 1;
	while(1) 
	{
		cout << "% ";
		getline(cin, cmdl);
		if (cmdl == "")
			continue;
		vector<string> cmdlSection;
		parse_into_sections(cmdl, cmdlSection); 
		for(int section_ind = 0; section_ind < cmdlSection.size(); section_ind++)
		{
			preparse(cmdlSection[section_ind], cmdvec);
			firc = 1;
			
			for(int j = 0; j < cmdvec.size(); j++)
			{
				parse_single_cmd(cmdvec[j]);
				
				create_pipes(cmdvec[j]);
				
				if (!firc)
				{
					for (int i = 0; i < cnt.size(); i++)
					{
						if (cnt[i] == -1)
						{
							cmdvec[j].in_index = rw[i][0]; //where input's from
							break;
						}
					}
					cmdvec[j].piped_in = 1;
				} 
				else 
				{
					for (int i = 0; i < cnt.size(); i++)
					{
						if (cnt[i] == 0) //where inputs from
						{
							cmdvec[j].in_index = rw[i][0];
							cmdvec[j].piped_in = 1;
							break;
						}
					}
				}
				
				execution(cmdvec[j]);
				
				firc = 0;
			}

			cmdvec.clear();
			
			for (int i = 0; i < cnt.size(); i++)
			{
				cnt[i]--;
			}
		}
	}

	return 0;
}

void execution(single_cmd &single_cmd){
	char* argarr[300];
	string file = single_cmd.args[0];
	///cout << "aaa" << file << "aaa" << endl;
	int cpid;

	if (file == "exit" || file == "EOF") 
	{
		std::exit(0);
	} 
	else if (file == "setenv")
	{
		setenv(single_cmd.args[1].c_str(), single_cmd.args[2].c_str(), 1);
	} 
	else if (file == "printenv")
	{
		if (getenv(single_cmd.args[1].c_str()) != NULL)
			cout << getenv(single_cmd.args[1].c_str()) << endl;
	} 
	else 
	{
		int status;
		while((cpid = fork()) < 0)
		{
			while(waitpid(-1, &status, WNOHANG) > 0);
		}
		if(cpid > 0) //parent
		{
			if (single_cmd.piped_in)
			{
				for (int i = 0; i < cnt.size(); i++)
				{
					if (rw[i][0] == single_cmd.in_index) //child's input is piped, so safe to close for parent
					{
						close(rw[i][0]);
						close(rw[i][1]);
						cnt.erase(cnt.begin()+i);
						rw.erase(rw.begin()+i);
						break;
					}
				}
			}
			if((file != "cat") || (file != "number") || (file != "removetag") || (file != "removetag0"))
			{
				waitpid(cpid, &status, 0);
			}
			else if (single_cmd.ppt == 0) //output is not piped
			{
				waitpid(cpid, &status, 0);
			}
			else 
			{
				waitpid(-1, &status, WNOHANG);
			}
		}
		else if(cpid == 0) //child
		{
			if (single_cmd.piped_in) //child input is piped
			{
				dup2(single_cmd.in_index, 0);
			}
			if (single_cmd.piped_out) //child output is piped
			{
				if (single_cmd.ppt == 1)
				{
					dup2(single_cmd.out_index, 1);
				} 
				else if (single_cmd.ppt == 2)
				{
					dup2(single_cmd.out_index, 1);
					dup2(single_cmd.out_index, 2);
				}
			}

			for (int i = 0; i < cnt.size(); i++)
			{
				close(rw[i][0]);
				close(rw[i][1]);
			}

			// file redirection
			int red;
			int ind;
			for (ind = 0; ind < single_cmd.args.size(); ind++)
			{
				if (single_cmd.args[ind] == ">")
					break;
			}
			if (ind == single_cmd.args.size()) ind = -1;

			if (ind == -1) //no redirection
			{
				if (single_cmd.ppt == 0)
					para_fix(single_cmd.args, argarr, (single_cmd.args).size());
				else
					para_fix(single_cmd.args, argarr, (single_cmd.args).size()-1); //last arg is '|'
			} 
			else 
			{
				red = open((single_cmd.args).back().c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
				dup2(red, 1); //red to stdout
				dup2(red, 2); //red to stderr
				close(red);
				para_fix(single_cmd.args, argarr, ind);
			}
			if (execvp(file.c_str(), argarr) == -1)
				cerr << "Unknown command: [" << file << "]." << endl;
			std::exit(0);
		}

	}
}

//parse cmdline into sections
void parse_into_sections(string cmdl, vector<string>& cmdlSection)
{
	int from = 0;
	while (cmdl != "")
	{
		int found = 0;
		for(int i = 0; i < cmdl.size(); ++i)
		{
			int j = i+1;
			if(cmdl[i] == '|' && cmdl[i+1] != ' ')
			{
				while(j < cmdl.size() && cmdl[j] != ' ') j++; //breaks if j out of bound or cmdline[j] is ' '
		 		j--;
				cmdlSection.push_back(cmdl.substr(from, j-from+1));
				cmdl = cmdl.substr(j+1);
				found = 1;
				break;
			}
		}

		if(!found)
		{
			cmdlSection.push_back(cmdl);
			break;
		}
	}
	
	//for(auto i: cmdlSection) cout << i << endl;
}

//parse cmdsec into single_cmds
void preparse(string cmdlsec, vector<single_cmd>& cmdvec)
{
	single_cmd temp_cmd;
	int from = 0;
	int end;
	cmdlsec.append("| ");
	
	while ((end = cmdlsec.find(' ', cmdlsec.find_first_of("|!", from))) != -1)
	{
		temp_cmd.in_index = 0;
		temp_cmd.out_index = 0;
		temp_cmd.ppt = 0;
		temp_cmd.piped_in = 0;
		temp_cmd.piped_out = 0;

		//parse cmdlsec
		temp_cmd.filename = cmdlsec.substr(from, end-from);
		while(temp_cmd.filename[0] == ' ') 
			temp_cmd.filename = temp_cmd.filename.substr(1);

		if (end == cmdlsec.size()-1) 
			temp_cmd.filename = temp_cmd.filename.substr(0, temp_cmd.filename.size()-1);
		while(temp_cmd.filename.back() == ' ') 
			temp_cmd.filename = temp_cmd.filename.substr(0, temp_cmd.filename.size()-1);
		cmdvec.push_back(temp_cmd);
		
		from = end + 1;
	}
	
	// for(auto i: cmdvec) 
	// {
	// 	cout << i.cmd << endl;
	// }
}

//parse singlle_cmd into args 
void parse_single_cmd(single_cmd &single_cmd)
{
	int from = 0;
	int end;
	single_cmd.filename.append(" ");
	
	while ((end = single_cmd.filename.find(" ", from)) != -1)
	{
		if (from == end) 
		{
			from = end + 1;
			continue;
		}
		single_cmd.args.push_back(single_cmd.filename.substr(from, end-from));
		from = end + 1;
	}
}

void para_fix(vector<string> &targs, char *arr[], int sz)
{
	for (int i=0; i < sz; i++)
	{
		arr[i] = (char*)targs[i].c_str();	
	}
	arr[targs.size()] = NULL;
}

//create a pipe based on single_cmd 
void create_pipes(single_cmd &single_cmd)
{
	int tempc = 0;
	int* temprw = new int[2];

	string endarg = single_cmd.args.back();

	if (regex_match(endarg, expression)) //number pipe
	{    
		if (endarg[0] == '|')
		{
			single_cmd.ppt = 1;
		} 
		else if (endarg[0] == '!')
		{
			single_cmd.ppt = 2;
		}
		tempc = stoi(endarg.substr(1, endarg.size()-1));
		bool need_new_pipe = 1;

		for (int i = 0; i < cnt.size(); i++)
		{
			if (tempc == cnt[i])
			{
				need_new_pipe = 0;
				single_cmd.out_index = rw[i][1];
				break;
			}
		}
		if (need_new_pipe)
		{
			pipe(temprw);
			single_cmd.out_index = temprw[1];
			cnt.push_back(tempc);
			rw.push_back(temprw);
		}
	} 
	else  
	{
		if (endarg == "|")
		{		    
			single_cmd.ppt = 1;
		} 
		else if (endarg == "!")
		{    
			single_cmd.ppt = 2;
		} 
		else  //no pipe 
		{						
			single_cmd.ppt = 0;
		}

		if (single_cmd.ppt != 0) //normal pipe
		{
			pipe(temprw);
			tempc = -1;
			single_cmd.out_index = temprw[1];
			cnt.push_back(tempc);
			rw.push_back(temprw);
			//cout << cnt.size() == rw.size() << endl;
		}
	}
	if (single_cmd.ppt != 0)
	{
		single_cmd.piped_out = 1;
	}
}


/*
cd cpp_codes/NPSHELL/
export PATH="$PATH:/home/daniel/cpp_codes/npshell"
cat commands.txt |3 ls |2 ls |1 number |1 number
ls |3 ls |2   cat commands.txt |1 number
cat commands.txt |2  cat test.html |1 number
*/