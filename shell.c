#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <dirent.h>

#define REDIRECT_NONE -1

#define REDIRECT_INPUT 0
#define REDIRECT_TRUNCATE 1
#define REDIRECT_APPEND 2

/*
 * This is a struct returned from parse_string
*/
typedef struct {
	int parts; // number of parts
	char** pointers; // pointers to each part of string
	int* lens; // lengths of each parts
	int* operators; // what operators between parts
} CommandData;

void command_data_add(CommandData* cd,char* pointer,int len,int operator);
void free_command_data(CommandData* cd);

/*
 * buffer for the command
*/
char buffer[4096];

/*
 * buffers for current path value and value to display
*/
char current_path_print[1100];
char current_path[1024];



void path_set_home() {
	char* home = getenv("HOME");
	snprintf(current_path,1024,"%s",home);
}

/*
 * go up in file system
*/
void path_go_up() {
	if (strlen(current_path) != 1) { // root
		int i = strlen(current_path) - 1;
		while (current_path[i] != '/') {
			i--;
		}
		
		if (i == 0) {
			current_path[1] = 0;
		}
		else {
			current_path[i] = 0;
		}
	}
}

/*
 * go into a directory
*/
void path_go_down(char* dirpath) {
	char path[900];
	memcpy(path,current_path,900);
	
	DIR* dir = opendir(".");
	struct dirent* dirst;
	while ((dirst = readdir(dir)) != NULL) {
		if (!strcmp(dirst->d_name,dirpath)) {
			snprintf(current_path,1024,"%s/%s",path,dirpath);
		}
	}
	closedir(dir);
}

/*
 * update printed buffer
*/
void update_current_path() {
	char* home = getenv("HOME");
	
	int homelen = strlen(home);
	
	if (!strncmp(current_path,home,homelen)) {
		snprintf(current_path_print,1100,"~%s",&current_path[homelen]);
	}
	else {
		snprintf(current_path_print,1024,"%s",current_path);
	}
}


/*
 * read one command line
*/
void read_line(char* buffer) {
	char c = 0;
	int index = 0;
	
	do { //read char by char until new line
		read(STDIN_FILENO,&c,1);
		buffer[index] = c;
		index++;
	} while (c != '\n');
	buffer[index - 1] = 0;
}

CommandData parse_string(char* str) {
	CommandData cd = {0};
	
	int i = 0;
	int start = 0;
	for (i = 0; str[i] != 0; i++) {
		if (str[i] == '<') {
			command_data_add(&cd,&str[start],i - start,REDIRECT_INPUT);
			start = i + 1;
		}
		else if (str[i] == '>') {
			if (str[i + 1] == '>') {
				command_data_add(&cd,&str[start],i - start,REDIRECT_APPEND);
				i++;
				start = i + 1;
			}
			else {
				command_data_add(&cd,&str[start],i - start,REDIRECT_TRUNCATE);
				start = i + 1;
			}
		}
	}
	command_data_add(&cd,&str[start],i - start,REDIRECT_NONE);
	
	return cd;
}

void command_data_add(CommandData* cd,char* pointer,int len,int operator) {
	int last = cd->parts;
	
	cd->parts++;
	cd->pointers = realloc(cd->pointers,sizeof(char*)*cd->parts);
	cd->lens = realloc(cd->lens,sizeof(int)*cd->parts);
	cd->operators = realloc(cd->operators,sizeof(int)*cd->parts);
	
	cd->pointers[last] = pointer;
	cd->lens[last] = len;
	cd->operators[last] = operator;
}

void free_command_data(CommandData* cd) {
	free(cd->pointers);
	free(cd->lens);
	free(cd->operators);
}

/*
 * This is a function to run a process and wait for exiting
*/
void run_process(char* command,char** args,int in_fd,int out_fd) {
	pid_t pid = fork();
	if (pid == 0) { //child process
		if (in_fd != -1) {
			dup2(in_fd,0);
		}
		if (out_fd != -1) {
			dup2(out_fd,1);
		}
		
		execvp(command,args);
	}
	else { // parent process
		int status;
		waitpid(pid,&status,0);
	}
}


int main(int argc,char** argv) {
	char string[1200];
	
	path_set_home();
	update_current_path();
	chdir(current_path);
	
	while (1) {
		snprintf(string,1200,"1730sh:%s$ ",current_path_print);
		
		write(STDOUT_FILENO,string,strlen(string));
		
		read_line(buffer);
		
		CommandData cd = parse_string(buffer);
		if (cd.parts != 0) {
			char** args = NULL;
			int arg_num = 0;
			char* command = NULL;
			
			int i;
			
			int buffer_len = strlen(buffer);
			
			for (i = 0; i < buffer_len; i++) {
				if (buffer[i] == ' ' || buffer[i] == '<' || buffer[i] == '>') {
					buffer[i] = 0;
				}
			}
			
			int command_started = 0;
			int command_ended = 0;
			int arg_started = 0;
			for (i = 0; i < cd.lens[0]; i++) {
				if (!command_started) {
					if (buffer[i] != 0) {
						command = &buffer[i];
						command_started = 1;
					}
				}
				else if (!command_ended) {
					if (buffer[i] == 0) {
						command_ended = 1;
					}
				}
				
				if (buffer[i] != 0) {
					if (!arg_started) {
						args = realloc(args,(arg_num + 1)*sizeof(char*));
						args[arg_num] = &buffer[i];
						arg_num++;
						
						arg_started = 1;
					}
				}
				else if (arg_started) {
					arg_started = 0;
				}
			}
			
			args = realloc(args,(arg_num + 1)*sizeof(char*));
			args[arg_num] = NULL; // set null at end, do mark end of arguments array
			
			if (command != NULL) {
				if (!strcmp(command,"exit")) {
					break;
				}
				else if (!strcmp(command,"cd")) {
					if (arg_num > 1) {
						if (!strcmp(args[1],"~")) {
							path_set_home();
						}
						else if (!strcmp(args[1],"..")) {
							path_go_up();
						}
						else {
							path_go_down(args[1]);
						}
					}
					else {
						path_set_home();
					}
					
					chdir(current_path);
					
					update_current_path();
				}
				else {
					if (cd.operators[0] == REDIRECT_NONE) {
						run_process(command,args,-1,-1);
					}
					else if (cd.parts == 2) {
						char* path = cd.pointers[1];
						while (path[0] == 0) { // if file path was starting with a space, skip it
							path++;
						}
						
						if (cd.operators[0] == REDIRECT_INPUT) {
							int in_fd = open(path,O_RDONLY);
							run_process(command,args,in_fd,-1);
						}
						else if (cd.operators[0] == REDIRECT_TRUNCATE) {
							int out_fd = open(path,O_WRONLY | O_TRUNC | O_CREAT);
							run_process(command,args,-1,out_fd);
						}
						else if (cd.operators[0] == REDIRECT_APPEND) {
							int out_fd = open(path,O_WRONLY | O_APPEND | O_CREAT);
							run_process(command,args,-1,out_fd);
						}
					}
					else if (cd.parts == 3) {
						int in_fd = -1;
						int out_fd = -1;
						
						int j;
						for (j = 0; j < 2; j++) {
							char* path = cd.pointers[j + 1];
							while (path[0] == 0) { // if file path was starting with a space, skip it
								path++;
							}
							
							if (cd.operators[j] == REDIRECT_INPUT) {
								in_fd = open(path,O_RDONLY);
							}
							else if (cd.operators[j] == REDIRECT_TRUNCATE) {
								out_fd = open(path,O_WRONLY | O_TRUNC | O_CREAT);
							}
							else if (cd.operators[j] == REDIRECT_APPEND) {
								out_fd = open(path,O_WRONLY | O_APPEND | O_CREAT);
							}
						}
						
						run_process(command,args,in_fd,out_fd);
					}
				}
			}
			
			free(args);
		}
		
		free_command_data(&cd);
	}
}
