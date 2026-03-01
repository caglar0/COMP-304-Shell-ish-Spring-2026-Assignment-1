# COMP-304-Shell-ish-Spring-2026-Assignment-1

GitHub Repository:
https://github.com/caglar0/COMP-304-Shell-ish-Spring-2026-Assignment-1

You can run the Makefile by typing "make" in the command line.

Builtin commands of shell-ish: 

cut: Prints specified fields of an input which are separated by a specific delimiter.
  Tests:
  cut -d " " -f1,3 <test.txt 
  cut -d ":" -f1,3 <test2.txt
  
chatroom: 
  Directory for chatroom folders: /tmp/chatroom-<roomname>
  Simple group chat using named pipes.

Custom Command: history
  Displays previously entered commands.
  Commands are stored before parsing so that the whole command is stored even if it includes piping, redirection and/or arguments.