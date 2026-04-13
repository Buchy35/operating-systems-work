Cole Buchinski
CSC 360 p1 - 10.10.2025

This project implements a simplified shell program which supports basic shell operations, including
Foreground / background execution, and directory navigation.

When starting the shell, it displays a prompt in the following format:

	username@hostname: /current/working/directory >

Type commands directly after the prompt.
Use Ctrl-D to exit.

FEATURES:

Basic Functions
	- Compiles successfully with make
	- Displays prompt in username@hostname: cwd > format
	- Exits cleanly on Ctrl-D

Foreground Execution
	- Executes external commands such as ls, pwd, etc.
	- Supports arguments
	- Long running programs, such as ping, can be interrupted with Ctrl-C
	- Prints error message upon non-existent command call

Directory Management
	- pwd prints current working directory
	- cd /absolute/path and cd relative/path work correctly
	- cd or cd ~ navigate to home directory
	- prompt reflects current directory

Background Execution
	- bg <command> [args] runs a program in the background
	- background jobs continue running while the shell accepts input
	- bglist lists all active background jobs with PIDs
	- when a background job finishes, shell indicates
	- terminated jobs removed from background list


