Parth Patel pvp72
Taha Touil tyt8

Overview
This document describes the testing strategy used to verify the functionality of the custom shell (mysh) in both batch mode and interactive mode. Testing focused on validating parsing, execution, built in commands, redirection, pipelines, wildcard expansion, and error handling.

Compilation
The program is compiled using the provided Makefile by running:
make

This produces the executable:
./mysh

Batch Mode Testing

Batch mode was tested using a file named test_input.txt containing a comprehensive set of commands that cover all required features.

To run batch mode:
./mysh test_input.txt

Test Categories and Observed Behavior

Built in Commands
Commands tested included pwd, cd, and which.
pwd correctly printed the current working directory.
cd successfully changed directories.
which ls printed the correct path to the executable.
which cd produced no output and returned exit status 1.

External Commands
Commands such as echo hello and cat a.txt executed correctly and produced expected output.

Output Redirection
The command echo alpha > a.txt created the file a.txt and wrote the correct contents.
Running cat a.txt printed alpha, confirming proper redirection behavior.

Input Redirection
The command wc < a.txt correctly read input from the file and produced the expected counts.

Wildcard Expansion
Commands echo .txt and echo foobar correctly expanded to matching filenames such as a.txt b.txt and foo1bar foo22bar.
For the edge case echo nope*.xyz, the output remained unchanged as nope*.xyz, which is correct when no matches are found.

Pipelines
Commands such as echo one two three | wc and pwd | wc executed correctly.
For example, echo one two three | wc produced output 1 3 14, indicating correct piping of data between processes.

Built in Commands in Pipelines
The command pwd | wc worked correctly, confirming that built in commands can participate in pipelines.

Syntax Error Handling
The command echo hi > produced the error message syntax error near >.
This confirms that the shell correctly detects missing filenames in redirection.

Command Not Found
The command not_a_real_command produced the message not_a_real_command: command not found and returned exit status 127, which matches standard Unix behavior.

Failure Recovery
Commands following errors, such as echo still_running, executed normally.
This confirms the shell continues running after encountering errors.

Exit Behavior
The command exit terminated the shell correctly.
The command echo hi | exit also terminated the shell, and the echo SHOULD_NOT_PRINT command was not executed, showing that exiting works even with pipes.

Interactive Mode Testing

Interactive mode was tested by running:
./mysh

The following behaviors were verified:

Prompt Display
The shell displayed a prompt showing the current working directory or a shortened path using ~ for the home directory.

Command Execution
Commands such as pwd and echo hello executed correctly.

Built in Commands
Commands cd, pwd, which ls, and which cd were tested.
which cd returned exit status 1, which is correct.

Error Handling
Entering not_a_real_command produced the expected error message and displayed Exited with status 127.

Syntax Errors
Entering echo hi > produced syntax error near > followed by Exited with status 1.

Pipelines
Commands such as echo one two three | wc produced correct output consistent with batch mode.

Exit
Entering exit terminated the shell and printed Exiting my shell.
