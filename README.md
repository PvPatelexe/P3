Parth Patel pvp72
Taha Touil tyt8

Overview:
This README is used to describe how to verify Project 3. It also describes all the tests that we did on mysh to make sure it works.
Compilation:
To compile, run this in the terminal: make, which will create the executable: ./mysh

Batch Mode Testing

Batch mode was tested using test_input.txt so we can check that the commands cover the required features.

To run batch mode:
./mysh test_input.txt

Tests:
Built in Commands:
pwd, cd, which
Outputs:
pwd printed the current working directory
cd changed directories
which ls printed the correct path to the executable
which cd produced no output and returned exit status 1

External Commands:
echo, hello, and cat a.txt gave the right output

Output Redirection:
The command echo alpha > a.txt created the file a.txt and wrote the correct contents
Running cat a.txt printed alpha, which is what we expected

Input Redirection:
The command wc < a.txt correctly read input from the file and printed the expected counts

Wildcard Expansion:
Commands echo .txt and echo foobar correctly expanded to matching filenames such as a.txt b.txt and foo1bar foo22bar.
For the edge case echo nope*.xyz, the output remained unchanged as nope*.xyz, which is correct when no matches are found.

Pipelines:
echo one two three | wc and pwd | wc executed correctly
echo one two three | wc produced output 1 3 14, which is what we expect

Pipeline with built in commands:
pwd | wc worked correctly, which is what we expected

Syntax Error Handling:
echo hi > produced the error message syntax error near >

Command Not Found:
not_a_real_command gave not_a_real_command: command not found and returned exit status 127

Failure Recovery:
echo still_running executed normally which is fine because we want it to go through errors

Exit Behavior:
The command exit terminated the shell
The command echo hi | exit also terminated the shell, and the echo SHOULD_NOT_PRINT command was not executed, which means that exiting works even with pipes

Interactive Mode Testing

Interactive mode was tested by running:
./mysh

We saw:

Prompt Display:
The shell displayed a prompt showing the current working directory or a shortened path using ~ for the home directory

Command Execution:
pwd and echo hello executed correctly

Built in Commands:
Commands cd, pwd, which ls, and which cd were tested
which cd returned exit status 1, which is what we expect

Error Handling:
Entering not_a_real_command gave the expected error message and displayed Exited with status 127

Syntax Errors:
Putting echo hi > produced syntax error near > and Exited with status 1

Pipelines:
echo one two three | wc produced the correct output

Exit:
Entering exit terminated the shell and printed Exiting my shell
