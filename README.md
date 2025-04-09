Utilized Linux Environment on cloudlab.us
Last updated 4/9/25

For Task 1:
gcc -o pa2_binary pa1_task2.c -pthread

Server Setup:
./pa2_binary server 127.0.0.1 12345

Client Set up: (creates 30 threads that send 10000 messages)
./pa2_binary client 127.0.0.1 12345 30 10000

For Task 2:
gcc -o pa2_binary pa2_task2.c -pthread

Server Setup:
./pa2_binary server 127.0.0.1 12345

Client Set up: (creates 30 threads that send 10000 messages)
./pa2_binary client 127.0.0.1 12345 30 10000
