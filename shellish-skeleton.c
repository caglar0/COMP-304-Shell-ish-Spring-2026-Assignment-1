#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h> //for open()
#include <dirent.h>   // DIR, opendir, readdir, closedir
#include <sys/stat.h> // mkdir, mkfifo

const char *sysname = "shellish";

//--------------PART 3c history command ------------------
#define HISTORY_SIZE 100
char *history[HISTORY_SIZE];
int history_count = 0;

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */


int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
      memset(c, 0, sizeof(struct command_t)); 
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  //------------ PART 3c history command----------------
  if (strlen(buf) > 0) {  //saves command before it is parsed to save commands with arguments, piping, redirection etc.
    history[history_count] = strdup(buf);
    history_count++;
  }
  //---------------------------------------------------
    
  parse_command(buf, command);

  //print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}

// ------------------- PART 1 --------------------------
void exec_command(struct command_t *command) {  // execv function
     // if the command contains '/' (eg. /bin/ls) call execv() directly 
    if (strchr(command->name, '/')) {  
      execv(command->name, command->args);
      perror("execv failed");
      exit(127);
    }

    // otherwise, we must manually search through PATH.
    char *path_env = getenv("PATH");   // Get PATH environment variable

    if (path_env == NULL) {
      fprintf(stderr, "PATH not found\n");
      exit(127);
    }

    char *path_copy = strdup(path_env); // strtok modifies the string, so work on a copy of PATH.

    // split PATH by ':'
    // example PATH:
    // /usr/local/bin:/usr/bin:/bin
    char *dir = strtok(path_copy, ":");

    while (dir != NULL) {

      // build full path string:
      // directory + "/" + command name
      char fullpath[1024];
      snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, command->name);
 
      if (access(fullpath, X_OK) == 0) { // check if file exists and is executable

        execv(fullpath, command->args);  // if executable found run it

        perror("execv failed");  // if execv returns it failed
        free(path_copy);
        exit(127);
      }
      dir = strtok(NULL, ":");
    }
    fprintf(stderr, "-%s: %s: command not found\n", sysname, command->name);  // if we exit the loop, command was not found anywhere in PATH.

    free(path_copy);
    exit(127);
  
}
//------------------ PART 2-cut ---------------
int cut_command(struct command_t *command) { // cut command implementation
  
  if (strcmp(command->name, "cut") == 0) {
    
    char delimiter = '\t'; //default delimiter is tab
    int fields[64];   
    int field_count = 0;    

    // parsing through args
    for (int i = 1; i < command->arg_count - 1; i++) {
      if (command->args[i] == NULL) break;

      if (strcmp(command->args[i], "-d") == 0 || strcmp(command->args[i], "--delimiter") == 0) {
	// handle the case where user typed -d " " (space delimiter)
	// the parser splits " " into two quote characters
	if (command->args[i+1][0] == '"' || command->args[i+1][0] == '\'') {
	  delimiter = ' ';
	  i++; //skip the space and quotation 
	} else {
	  delimiter = command->args[i+1][0];
	  i++;    // skip the delimiter since it is already obtained
	}
      }

      if (strncmp(command->args[i], "-f", 2) == 0 || strcmp(command->args[i], "--fields") == 0) {  //strncmp because -f1,3,6 is count as one arg
	char fields_copy[64];  //work on a copy to not modify the args field
   	
	if (strncmp(command->args[i], "-f", 2) == 0) {
	    strncpy(fields_copy, command->args[i] + 2, sizeof(fields_copy) - 1); // points to 1,3,6 part of -f1,3,6 
	  } else {
	    strncpy(fields_copy, command->args[i+1], sizeof(fields_copy) - 1);
	    i++;
	  }
	fields_copy[sizeof(fields_copy) - 1] = '\0';  // null terminate

	char *token = strtok(fields_copy, ",");
	while (token != NULL) {
	  fields[field_count] = atoi(token) - 1; // convert to 0-indexed and store 
	  token = strtok(NULL, ",");
	  field_count++;
	}
      }
    }

    char line[4096];
    char *tokens[256];
    char *token;
    char delim_str[2] = {delimiter, '\0'};
    
    while (fgets(line, sizeof(line), stdin)) {  //loop over each line
      int token_count = 0; 
      
      // strip trailing newline
      line[strcspn(line ,"\n")] = '\0';
      
      // tokenize the line by delimiter
      // store tokens in char *tokens[256], count them
      token = strtok(line, delim_str);
      while (token != NULL) { 
	tokens[token_count] = token; 
	token = strtok(NULL, delim_str);
	token_count++;
      }

      // print the requested fields separated by delimiter
      // print newline at the end
      for (int j = 0; j < field_count; j++) {
	if (fields[j] < token_count) {  // makes sure you don't try to access a token that doesn't exist
	  printf("%s", tokens[fields[j]]);
	  
	  if (j < field_count - 1) {
	    printf("%c", delimiter);
	  }
        }
      }
    printf("\n");
    }
  }
  return SUCCESS;
}

//------------- PART 3-chatroom --------------------- 
void join_chatroom(struct command_t *command) {
  
  if (command->arg_count < 4) {  // validates that rommname and username is provided
    printf("Usage: chatroom <roomname> <username>\n"); 
      return;
  }

  char *roomname = command->args[1];
  char *username = command->args[2];

  char room_path[256];
  snprintf(room_path, sizeof(room_path), "/tmp/chatroom-%s", roomname);  // build the room folder path /tmp/chatroom-<roomname>
 

  // create room folder if it doesn't exist
  mkdir(room_path, 0777);

  // build user pipe path and create it /tmp/chatroom-<roomname>/<username>
  char user_pipe[512];
  snprintf(user_pipe, sizeof(user_pipe), "%s/%s", room_path, username);
  mkfifo(user_pipe, 0666);
  printf("Welcome to %s!\n", roomname);

  // fork the process one for read one for write
  pid_t pid = fork();

  //-------CHILD--------
  if (pid == 0) {
    // child process is for reading
    // opens your users named pipe and prints incoming messages
    
    int fd = open(user_pipe, O_RDONLY);  // open your own pipe for reading
    char buf[1024];  // temp storage for read messages from the pipe
    while (1) {  // always waits for new messages
      int n = read(fd, buf, sizeof(buf) - 1);  // read message from pipe into buf, n is number of bytes read
      if (n > 0) {
	buf[n] = '\0';  // null terminate the mesage 
	buf[strcspn(buf, "\n")] = '\0';  //strip newline
	
        printf("\r\033[K[%s] %s\n", roomname, buf);  // erases the the previous prompt and prints the message received 
        fflush(stdout);

	// reprint the prompt after receiving a message
        printf("[%s] %s > ", roomname, username); 
        fflush(stdout);
      }
    }
        close(fd);
        exit(0);
	
  } else {
    //-------PARENT--------
        // parent process for writing
    char input[1024]; //input buffer
        while (1) {
	  printf("[%s] %s > ", roomname, username); 
          fflush(stdout);

            if (fgets(input, sizeof(input), stdin) == NULL) break;  //breaks if stdin closes 
            input[strcspn(input, "\n")] = '\0';  // strip newline
	    if (strlen(input) == 0) continue; // skip empty message

	    // exit the chatroom if user types exit
            if (strcmp(input, "exit") == 0) break;

            // build message: "username: message"
            char message[1280];
            snprintf(message, sizeof(message), "%s: %s", username, input);

            DIR *dir = opendir(room_path); //open room folder 
            if (dir == NULL) break;

            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {  // iterate over each named pipe in the room folder
              // skip your own pipe
	      if (strcmp(entry->d_name, username) == 0) continue; 

              // build path to other user's named pipe
              char other_pipe[512];
              snprintf(other_pipe, sizeof(other_pipe), "%s/%s", room_path, entry->d_name);

              // fork a child to write open() on a pipe blocks until
              // someone reads, so we fork to avoid freezing the shell
		
              pid_t writer_pid = fork();
              if (writer_pid == 0) {
		int wfd = open(other_pipe, O_WRONLY);  // open other user's named pipe for writing
	        if (wfd != -1) {  // if open
		write(wfd, message, strlen(message));  // write the message then close file desc
	        close(wfd);  
                }
                exit(0); 
	      }
            }
            closedir(dir);
        }
        // kill the reader child when exiting
        kill(pid, SIGTERM); 
        waitpid(pid, NULL, 0);  //remove zombie's entry from process table (reaped)
    }
	
}
  
int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

  if (strcmp(command->name, "chatroom") == 0) {
      join_chatroom(command);
      return SUCCESS;
  }

  // ------------------ PART 3c history command-----------

  if (strcmp(command->name, "history") == 0) {
      for (int i = 0; i < history_count; i++) {
	printf("%d %s\n", i+1, history[i]);
      }
      return SUCCESS;
      }
      
  // PART 2-piping
  if (command->next) {
    int fd[2];
    pipe(fd); //create the pipe

    // fork left child (write end)
    pid_t pid_left = fork();
    
    if (pid_left == 0) {
      dup2(fd[1], STDOUT_FILENO);
      close(fd[0]);
      close(fd[1]);

      if (strcmp(command->name, "cut") == 0) {  // for calls with piping (cat /etc/passwd | cut -d ":" -f1,6)
        cut_command(command);
	exit(0);
	}
       
      exec_command(command);
      exit(127);
    }

     // fork right child (read end)
    pid_t pid_right = fork();
    
    if (pid_right == 0) {
      dup2(fd[0], STDIN_FILENO);
      close(fd[1]);
      close(fd[0]);

      exit(process_command(command->next));
    }

    // parent must close BOTH ends
    close(fd[0]);
    close(fd[1]);

    // wait for both children
    waitpid(pid_left, NULL, 0);
    waitpid(pid_right, NULL, 0);
    return SUCCESS;
  }
  
  pid_t pid = fork();
  if (pid == 0) // child
  {

    //PART 2-redirection:

    // input redirection <
    if (command->redirects[0]) {
      int fd = open(command->redirects[0], O_RDONLY);
      
      dup2(fd, STDIN_FILENO);
      close(fd);
    }

    // output redirection >
    if (command->redirects[1]) {
      int fd = open(command->redirects[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);

      dup2(fd, STDOUT_FILENO);
      close(fd);
    }

    // output redirection >>
    if (command->redirects[2]) {
      int fd = open(command->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644);

      dup2(fd, STDOUT_FILENO);
      close(fd);
    }

    if (strcmp(command->name, "cut") == 0) {  // for calls with redirection (cut -d ":" -f1,3 <test.txt)
      cut_command(command);
      exit(0);
      }	

    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    /*execvp(command->name, command->args); // exec+args+path
    printf("-%s: %s: command not found\n", sysname, command->name);
    exit(127);*/

    exec_command(command);
    exit(127);
    printf("-%s: %s: command not found\n", sysname, command->name);

  } else {
    // TODO: implement background processes here
    //wait(0); // wait for child process to finish

    if (command->background){ //if command is called with & parent doesnt wait child
      printf("background pid %d\n", pid);
    } else {
      waitpid(pid, NULL, 0); // parent waits for child.
    }
    return SUCCESS;
  }
}

int main() {
  while (1) {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}
